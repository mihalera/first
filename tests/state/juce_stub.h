// A stand-in for the toolkit the processor's parameter and saved-state code is
// written against, so the round trip can be measured with nothing but a C++17
// compiler.
//
// SCOPE. Everything here is modelled to the extent the SHIPPING code under test
// touches it, and no further:
//
//   var / ValueTree / XmlElement  - the saved-state shape. A tree is a type name,
//       a property map and children; the handle is shared the way JUCE's is and
//       createCopy() is the deep copy the plugin's A/B slots and preset paths
//       take. A tree reaches the session through the XML element, and the
//       harness carries the numbers across it at FULL float precision - JUCE
//       prints fewer digits than this, so a value that survives here survives
//       there too.
//
//   copyXmlToBinary / getXmlFromBinary - the two ends the plugin hands its state
//       through. The block carries a magic header, so a block that did not come
//       from copyXmlToBinary() is rejected with a null pointer exactly as JUCE
//       rejects XML that does not parse - which is the path the plugin's own
//       null guard exists for.
//
//   NormalisableRange / AudioParameterFloat / AudioParameterChoice /
//       AudioParameterBool / ParameterLayout / AudioProcessorValueTreeState -
//       the parameter table. The layout the plugin's createParameterLayout()
//       builds is a REAL table here: it is constructed from the shipping text, so
//       the ids, the ranges, the choices and the defaults are the plugin's own.
//
//       copyState() writes one <PARAM id value> child per parameter and
//       replaceState() applies them back through each parameter's own range,
//       which is what JUCE's AudioProcessorValueTreeState does with a session.
//
// What is NOT modelled: the normalisable-range skew maths (nothing on the
// saved-state path normalises a value), parameter listeners, attachments and the
// undo manager. The harness fails to compile if the shipping code reaches for
// one of those.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace juce
{

class XmlElement;
class MemoryBlock;

template <typename T>
constexpr T jmin (T a, T b) noexcept { return b < a ? b : a; }

template <typename T>
constexpr T jmax (T a, T b) noexcept { return a < b ? b : a; }

template <typename T>
constexpr T jlimit (T lo, T hi, T v) noexcept { return v < lo ? lo : (hi < v ? hi : v); }

template <typename T>
struct MathConstants
{
    static constexpr T pi = static_cast<T> (3.14159265358979323846);
};

/** The harness compiles the shipping text with warnings as errors in places, so
    the innocuous ignoreUnused the plugin uses must resolve here too. */
template <typename... Ts>
inline void ignoreUnused (Ts&&...) noexcept {}

//==============================================================================
/** A UTF-8 literal. The language choice list is built from one, so the type has
    to exist and keep its bytes; nothing on the saved-state path decodes it. */
class CharPointer_UTF8
{
public:
    explicit CharPointer_UTF8 (const char* textIn) : text (textIn != nullptr ? textIn : "") {}

    const char* getAddress() const noexcept { return text; }

private:
    const char* text;
};

class String
{
public:
    String() = default;
    String (const char* textIn) : text (textIn != nullptr ? textIn : "") {}
    String (const std::string& textIn) : text (textIn) {}
    String (CharPointer_UTF8 pointer) : text (pointer.getAddress()) {}

    bool isEmpty() const noexcept { return text.empty(); }
    bool isNotEmpty() const noexcept { return ! text.empty(); }

    const std::string& toStdString() const noexcept { return text; }
    const char* toRawUTF8() const noexcept { return text.c_str(); }

    bool operator== (const String& other) const noexcept { return text == other.text; }
    bool operator!= (const String& other) const noexcept { return text != other.text; }
    bool operator< (const String& other) const noexcept { return text < other.text; }

private:
    std::string text;
};

class StringArray
{
public:
    StringArray() = default;
    StringArray (std::initializer_list<String> items) : values (items) {}

    void add (const String& value) { values.push_back (value); }
    int size() const noexcept { return static_cast<int> (values.size()); }
    bool isEmpty() const noexcept { return values.empty(); }
    const String& operator[] (int index) const { return values[static_cast<std::size_t> (index)]; }

private:
    std::vector<String> values;
};

//==============================================================================
/** The property value type. Only the shapes a parameter state uses: a number
    (int or double), a bool, or a string. */
class var
{
public:
    enum class Kind { nothing, integer, number, boolean, text };

    var() = default;
    var (int value) : kind (Kind::integer), integer (value), number (value) {}
    var (float value) : kind (Kind::number), integer (static_cast<int> (value)), number (value) {}
    var (double value) : kind (Kind::number), integer (static_cast<int> (value)), number (value) {}
    var (bool value) : kind (Kind::boolean), integer (value ? 1 : 0), number (value ? 1.0 : 0.0) {}
    var (const char* text) : kind (Kind::text), string (String (text)) {}
    var (const String& text) : kind (Kind::text), string (text) {}

    explicit operator int() const noexcept { return integer; }
    explicit operator float() const noexcept { return static_cast<float> (number); }
    explicit operator double() const noexcept { return number; }
    explicit operator bool() const noexcept { return integer != 0; }

    bool isVoid() const noexcept { return kind == Kind::nothing; }
    bool isInt() const noexcept { return kind == Kind::integer; }
    bool isDouble() const noexcept { return kind == Kind::number; }
    bool isBool() const noexcept { return kind == Kind::boolean; }
    bool isString() const noexcept { return kind == Kind::text; }

    /** The value as the session stores it. A number is written at full precision
        and an integer as digits, so a value cannot round-trip to something else. */
    std::string toXmlText() const;

    /** The inverse: a number when the text is one it could have written, a string
        otherwise. */
    static var fromXmlText (const std::string& text);

    const String& asString() const noexcept { return string; }

private:
    Kind kind = Kind::nothing;
    int integer = 0;
    double number = 0.0;
    String string;
};

//==============================================================================
struct ValueTreeData;

/** A value tree with JUCE's reference semantics: copies share a node until one of
    them writes to it, and createCopy() is the deep copy the plugin takes when it
    stores a state. */
class ValueTree
{
public:
    ValueTree() = default;
    ValueTree (const String& type);
    ValueTree (const char* type) : ValueTree (String (type)) {}

    bool isValid() const noexcept { return data != nullptr; }
    bool isEquivalentTo (const ValueTree& other) const;
    ValueTree createCopy() const;

    const String& getType() const noexcept;

    void setProperty (const String& name, const var& value, void* undoManager = nullptr);
    var getProperty (const String& name) const;
    var getProperty (const String& name, const var& fallback) const;
    bool hasProperty (const String& name) const;

    int getNumChildren() const;
    ValueTree getChild (int index) const;
    ValueTree getChildWithProperty (const String& name, const var& value) const;
    void addChild (const ValueTree& child, int index);
    void removeAllChildren();

    std::unique_ptr<XmlElement> createXml() const;
    static ValueTree fromXml (const XmlElement& xml);

    /** The harness's own serializer hook. JUCE cannot enumerate a tree's
        properties - its XML element is what carries them - so the block writer
        asks for them here, and nothing else in the harness does. */
    const std::map<std::string, var>& harnessProperties() const;

private:
    std::shared_ptr<ValueTreeData> data;
};

//==============================================================================
/** The element a state travels in. The harness keeps the tree inside it rather
    than writing XML an application could read: the plugin never inspects the
    element, it only hands it back to the toolkit, so what has to survive the trip
    is the tree. copyXmlToBinary() writes that tree out, and getXmlFromBinary()
    reads it back or refuses the block. */
class XmlElement
{
public:
    explicit XmlElement (const String& tag) : name (tag) {}

    const String& getTagName() const noexcept { return name; }

    void setTree (const ValueTree& tree);
    const ValueTree& getTree() const noexcept { return payload; }

private:
    String name;
    ValueTree payload;
};

class MemoryBlock
{
public:
    MemoryBlock() = default;

    void setSize (std::size_t newSize) { bytes.resize (newSize); }
    std::size_t getSize() const noexcept { return bytes.size(); }
    void* getData() noexcept { return bytes.empty() ? nullptr : bytes.data(); }
    const void* getData() const noexcept { return bytes.empty() ? nullptr : bytes.data(); }

    std::vector<unsigned char>& raw() noexcept { return bytes; }
    const std::vector<unsigned char>& raw() const noexcept { return bytes; }

private:
    std::vector<unsigned char> bytes;
};

/** The base class the two state helpers hang off.

    In JUCE they are STATIC MEMBERS of juce::AudioProcessor, not free functions -
    which is why the plugin's own getStateInformation() can call them
    unqualified from a class that derives from it. The harness models that shape,
    so the shipping text compiles against the same thing it compiles against in
    the build rather than against a global function the plugin does not have. */
class AudioProcessor
{
public:
    /** Writes the element's tree into the block. */
    static void copyXmlToBinary (const XmlElement& xml, MemoryBlock& destData);

    /** Reads a block written by the above; null for anything else, which is the
        path the plugin's own null guard exists for. */
    static std::unique_ptr<XmlElement> getXmlFromBinary (const void* data, int sizeInBytes);
};

//==============================================================================
/** A range of values, as the parameter table builds them. The skew is recorded
    but never applied: nothing on the saved-state path normalises a value, and a
    snap-to-interval is what a state load actually needs. */
template <typename Type>
class NormalisableRange
{
public:
    NormalisableRange() = default;

    NormalisableRange (Type rangeStart, Type rangeEnd, Type intervalValue = 0)
        : start (rangeStart), end (rangeEnd), interval (intervalValue) {}

    void setSkewForCentre (Type centre) { centrePoint = centre; }

    Type snapToLegalValue (Type value) const
    {
        if (interval > 0)
            value = start + interval * static_cast<Type> (std::llround (static_cast<double> (value - start)
                                                                        / static_cast<double> (interval)));
        return jlimit (start, end, value);
    }

    Type getStart() const noexcept { return start; }
    Type getEnd() const noexcept { return end; }

    Type start = 0;
    Type end = 1;
    Type interval = 0;
    Type centrePoint = 0;
};

//==============================================================================
struct ParameterID
{
    ParameterID (String idIn, int versionIn) : id (std::move (idIn)), version (versionIn) {}

    String id;
    int version;
};

class AudioParameterFloatAttributes
{
public:
    AudioParameterFloatAttributes withLabel (const String& labelIn) const
    {
        auto copy = *this;
        copy.unit = labelIn;
        return copy;
    }

    const String& label() const noexcept { return unit; }

private:
    String unit;
};

//==============================================================================
/** The base of the three parameter kinds the table uses. */
class RangedAudioParameter
{
public:
    virtual ~RangedAudioParameter() = default;

    const ParameterID& getParameterID() const noexcept { return id; }
    const String& getName() const noexcept { return name; }

    /** Denormalised, which is what a saved state stores and what the engine reads. */
    virtual float getValue() const noexcept = 0;
    virtual void setValue (float newValue) noexcept = 0;
    virtual float getDefaultValue() const noexcept = 0;

    virtual bool isChoice() const noexcept { return false; }
    virtual int getNumChoices() const noexcept { return 0; }
    virtual const String& getChoiceName (int) const;

protected:
    RangedAudioParameter (ParameterID idIn, const String& nameIn)
        : id (std::move (idIn)), name (nameIn) {}

    ParameterID id;
    String name;
};

class AudioParameterFloat : public RangedAudioParameter
{
public:
    AudioParameterFloat (ParameterID idIn, const String& nameIn, NormalisableRange<float> rangeIn,
                         float defaultValueIn,
                         AudioParameterFloatAttributes attributesIn = {})
        : RangedAudioParameter (std::move (idIn), nameIn),
          range (rangeIn),
          attributes (std::move (attributesIn)),
          value (range.snapToLegalValue (defaultValueIn)),
          defaultValue (range.snapToLegalValue (defaultValueIn)) {}

    float getValue() const noexcept override { return value; }
    void setValue (float newValue) noexcept override { value = range.snapToLegalValue (newValue); }
    float getDefaultValue() const noexcept override { return defaultValue; }

    const NormalisableRange<float>& getRange() const noexcept { return range; }
    const AudioParameterFloatAttributes& getAttributes() const noexcept { return attributes; }

private:
    NormalisableRange<float> range;
    AudioParameterFloatAttributes attributes;
    float value;
    float defaultValue;
};

class AudioParameterBool : public RangedAudioParameter
{
public:
    AudioParameterBool (ParameterID idIn, const String& nameIn, bool defaultValueIn)
        : RangedAudioParameter (std::move (idIn), nameIn),
          value (defaultValueIn ? 1.0f : 0.0f),
          defaultValue (value) {}

    float getValue() const noexcept override { return value; }
    void setValue (float newValue) noexcept override { value = newValue >= 0.5f ? 1.0f : 0.0f; }
    float getDefaultValue() const noexcept override { return defaultValue; }

private:
    float value;
    float defaultValue;
};

class AudioParameterChoice : public RangedAudioParameter
{
public:
    AudioParameterChoice (ParameterID idIn, const String& nameIn, StringArray choicesIn, int defaultIndexIn)
        : RangedAudioParameter (std::move (idIn), nameIn),
          choices (std::move (choicesIn)),
          index (jlimit (0, jmax (0, choices.size() - 1), defaultIndexIn)),
          defaultIndex (index) {}

    float getValue() const noexcept override { return static_cast<float> (index); }

    void setValue (float newValue) noexcept override
    {
        index = jlimit (0, jmax (0, choices.size() - 1), static_cast<int> (std::lround (newValue)));
    }

    float getDefaultValue() const noexcept override { return static_cast<float> (defaultIndex); }

    bool isChoice() const noexcept override { return true; }
    int getNumChoices() const noexcept override { return choices.size(); }
    const String& getChoiceName (int choiceIndex) const override
    {
        return choices[jlimit (0, jmax (0, choices.size() - 1), choiceIndex)];
    }

    const StringArray& getChoices() const noexcept { return choices; }

private:
    StringArray choices;
    int index;
    int defaultIndex;
};

//==============================================================================
class AudioProcessorValueTreeState
{
public:
    struct ParameterLayout
    {
        void add (std::unique_ptr<RangedAudioParameter> parameter)
        {
            parameters.push_back (std::move (parameter));
        }

        std::vector<std::unique_ptr<RangedAudioParameter>> parameters;
    };

    AudioProcessorValueTreeState (const String& typeIn, ParameterLayout layout)
        : type (typeIn)
    {
        for (auto& parameter : layout.parameters)
            parameters.push_back (std::move (parameter));
    }

    /** One <PARAM id value> child per parameter, which is the shape JUCE's own
        AudioProcessorValueTreeState writes into a session. */
    ValueTree copyState() const;

    /** Applies a session's tree back onto the parameters: match by id, clamp
        through the parameter's own range. Children without an id, and ids no
        parameter answers to, are ignored - as the real one ignores them. */
    void replaceState (const ValueTree& state);

    int getNumParameters() const noexcept { return static_cast<int> (parameters.size()); }

    RangedAudioParameter* getParameterAt (int index) const noexcept
    {
        if (index < 0 || index >= getNumParameters())
            return nullptr;
        return parameters[static_cast<std::size_t> (index)].get();
    }

    RangedAudioParameter* getParameter (const String& id) const noexcept
    {
        for (const auto& parameter : parameters)
            if (parameter->getParameterID().id == id)
                return parameter.get();
        return nullptr;
    }

private:
    String type;
    std::vector<std::unique_ptr<RangedAudioParameter>> parameters;
};

} // namespace juce
