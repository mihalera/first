// The behaviour behind tests/state/juce_stub.h: the value tree, the element a
// state travels in, and the block it travels through.
//
// A tree is written as an indented listing - the type, then the node's own
// properties, then its children, each one tab deeper - and read back by the
// matching recursive reader. That listing is the harness's stand-in for JUCE's
// XML and for the binary framing around it. The plugin never reads either one;
// what has to survive the trip is the TREE, and the listing keeps every property
// at full float precision, every child in order and the type name intact.
#include "juce_stub.h"

#include <sstream>

namespace juce
{

namespace
{
    constexpr const char* blockMagic = "J37-STATE-BLOCK-1\n";

    std::string escape (const std::string& field)
    {
        std::string escaped;

        for (const auto character : field)
        {
            if (character == '\\')
                escaped += "\\\\";
            else if (character == '\n')
                escaped += "\\n";
            else if (character == '\t')
                escaped += "\\t";
            else
                escaped += character;
        }

        return escaped;
    }

    std::string unescape (const std::string& field)
    {
        std::string plain;

        for (std::size_t i = 0; i < field.size(); ++i)
        {
            if (field[i] == '\\' && i + 1 < field.size())
            {
                const auto next = field[++i];
                plain += next == 'n' ? '\n' : (next == 't' ? '\t' : next);
            }
            else
            {
                plain += field[i];
            }
        }

        return plain;
    }

    std::vector<std::string> splitTabs (const std::string& line)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;

        while (true)
        {
            const auto tab = line.find ('\t', start);
            fields.push_back (line.substr (start, tab == std::string::npos ? std::string::npos : tab - start));

            if (tab == std::string::npos)
                break;

            start = tab + 1;
        }

        return fields;
    }

    void writeTree (std::ostringstream& out, const ValueTree& tree, int depth)
    {
        const auto indent = std::string (static_cast<std::size_t> (depth), '\t');
        out << indent << "T\t" << escape (tree.getType().toStdString()) << "\n";

        for (const auto& property : tree.harnessProperties())
            out << indent << "\tP\t" << escape (property.first) << "\t" << escape (property.second.toXmlText()) << "\n";

        for (int i = 0; i < tree.getNumChildren(); ++i)
            writeTree (out, tree.getChild (i), depth + 1);
    }

    struct Reader
    {
        explicit Reader (std::string textIn) : text (std::move (textIn)) {}

        bool atEnd() const { return position >= text.size(); }

        std::string peek() const
        {
            const auto end = text.find ('\n', position);
            return text.substr (position, end == std::string::npos ? std::string::npos : end - position);
        }

        std::string take()
        {
            const auto line = peek();
            const auto end = text.find ('\n', position);
            position = end == std::string::npos ? text.size() : end + 1;
            return line;
        }

        std::string text;
        std::size_t position = 0;
    };

    int depthOf (const std::string& line)
    {
        int depth = 0;
        while (depth < static_cast<int> (line.size()) && line[static_cast<std::size_t> (depth)] == '\t')
            ++depth;
        return depth;
    }

    ValueTree readNode (Reader& reader, int depth);
}

//==============================================================================
struct ValueTreeData
{
    String type;
    std::map<std::string, var> properties;
    std::vector<ValueTree> children;
};

const String& RangedAudioParameter::getChoiceName (int) const
{
    static const String empty;
    return empty;
}

std::string var::toXmlText() const
{
    switch (kind)
    {
        case Kind::integer: return std::to_string (integer);
        case Kind::number:
        {
            char buffer[40];
            std::snprintf (buffer, sizeof (buffer), "%.17g", number);
            return buffer;
        }
        case Kind::boolean: return integer != 0 ? "1" : "0";
        case Kind::text:    return string.toStdString();
        case Kind::nothing: break;
    }
    return {};
}

var var::fromXmlText (const std::string& text)
{
    if (text.empty())
        return {};

    char* end = nullptr;
    const auto parsed = std::strtod (text.c_str(), &end);

    if (end != nullptr && *end == '\0')
    {
        const auto asNumber = var (parsed);
        if (asNumber.toXmlText() == text)
            return asNumber;
    }

    return var (String (text));
}

//==============================================================================
ValueTree::ValueTree (const String& type) : data (std::make_shared<ValueTreeData>())
{
    data->type = type;
}

const String& ValueTree::getType() const noexcept
{
    static const String empty;
    return data != nullptr ? data->type : empty;
}

const std::map<std::string, var>& ValueTree::harnessProperties() const
{
    static const std::map<std::string, var> empty;
    return data != nullptr ? data->properties : empty;
}

bool ValueTree::isEquivalentTo (const ValueTree& other) const
{
    if (data == other.data)
        return true;

    if (! isValid() || ! other.isValid())
        return false;

    if (data->type != other.data->type || data->properties.size() != other.data->properties.size())
        return false;

    for (const auto& entry : data->properties)
    {
        const auto found = other.data->properties.find (entry.first);

        if (found == other.data->properties.end() || found->second.toXmlText() != entry.second.toXmlText())
            return false;
    }

    if (getNumChildren() != other.getNumChildren())
        return false;

    for (int i = 0; i < getNumChildren(); ++i)
        if (! getChild (i).isEquivalentTo (other.getChild (i)))
            return false;

    return true;
}

ValueTree ValueTree::createCopy() const
{
    if (! isValid())
        return {};

    ValueTree copy (data->type);
    copy.data->properties = data->properties;

    for (const auto& child : data->children)
        copy.data->children.push_back (child.createCopy());

    return copy;
}

void ValueTree::setProperty (const String& name, const var& value, void*)
{
    if (data != nullptr)
        data->properties[name.toStdString()] = value;
}

var ValueTree::getProperty (const String& name) const
{
    return getProperty (name, var());
}

var ValueTree::getProperty (const String& name, const var& fallback) const
{
    if (data == nullptr)
        return fallback;

    const auto found = data->properties.find (name.toStdString());
    return found == data->properties.end() ? fallback : found->second;
}

bool ValueTree::hasProperty (const String& name) const
{
    return data != nullptr && data->properties.count (name.toStdString()) > 0;
}

int ValueTree::getNumChildren() const
{
    return data != nullptr ? static_cast<int> (data->children.size()) : 0;
}

ValueTree ValueTree::getChild (int index) const
{
    if (data == nullptr || index < 0 || index >= getNumChildren())
        return {};

    return data->children[static_cast<std::size_t> (index)];
}

ValueTree ValueTree::getChildWithProperty (const String& name, const var& value) const
{
    for (int i = 0; i < getNumChildren(); ++i)
    {
        const auto child = getChild (i);

        if (child.hasProperty (name) && child.getProperty (name).toXmlText() == value.toXmlText())
            return child;
    }

    return {};
}

void ValueTree::addChild (const ValueTree& child, int index)
{
    if (data == nullptr || ! child.isValid())
        return;

    if (index < 0 || index > getNumChildren())
        data->children.push_back (child);
    else
        data->children.insert (data->children.begin() + index, child);
}

void ValueTree::removeAllChildren()
{
    if (data != nullptr)
        data->children.clear();
}

std::unique_ptr<XmlElement> ValueTree::createXml() const
{
    if (! isValid())
        return nullptr;

    auto element = std::make_unique<XmlElement> (data->type);
    element->setTree (*this);
    return element;
}

ValueTree ValueTree::fromXml (const XmlElement& xml)
{
    return xml.getTree();
}

void XmlElement::setTree (const ValueTree& tree)
{
    payload = tree;
}

//==============================================================================
namespace
{
    ValueTree readNode (Reader& reader, int depth)
    {
        if (reader.atEnd() || depthOf (reader.peek()) != depth)
            return {};

        const auto header = reader.take();
        const auto fields = splitTabs (header.substr (static_cast<std::size_t> (depth)));

        if (fields.size() < 2 || fields[0] != "T")
            return {};

        ValueTree node (unescape (fields[1]));

        // The node's own properties are the `P` lines at depth + 1; its children
        // are the `T` lines there, and each of those recurses.
        while (! reader.atEnd() && depthOf (reader.peek()) == depth + 1)
        {
            const auto line = reader.peek();
            const auto fields2 = splitTabs (line.substr (static_cast<std::size_t> (depth) + 1));

            if (fields2.empty() || fields2[0] != "P" || fields2.size() < 3)
                break;

            reader.take();
            node.setProperty (String (unescape (fields2[1])), var::fromXmlText (unescape (fields2[2])), nullptr);
        }

        while (! reader.atEnd() && depthOf (reader.peek()) == depth + 1)
            node.addChild (readNode (reader, depth + 1), -1);

        return node;
    }
}

void AudioProcessor::copyXmlToBinary (const XmlElement& xml, MemoryBlock& destData)
{
    std::ostringstream out;
    out << blockMagic;
    writeTree (out, xml.getTree(), 0);

    const auto text = out.str();
    destData.raw().assign (text.begin(), text.end());
}

std::unique_ptr<XmlElement> AudioProcessor::getXmlFromBinary (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return nullptr;

    const auto* bytes = static_cast<const unsigned char*> (data);
    const std::string text (reinterpret_cast<const char*> (bytes), static_cast<std::size_t> (sizeInBytes));

    if (text.compare (0, std::strlen (blockMagic), blockMagic) != 0)
        return nullptr;

    Reader reader (text.substr (std::strlen (blockMagic)));
    const auto tree = readNode (reader, 0);

    if (! tree.isValid())
        return nullptr;

    auto element = std::make_unique<XmlElement> (tree.getType());
    element->setTree (tree);
    return element;
}

//==============================================================================
ValueTree AudioProcessorValueTreeState::copyState() const
{
    ValueTree tree (type);

    for (const auto& parameter : parameters)
    {
        ValueTree child ("PARAM");
        child.setProperty ("id", var (parameter->getParameterID().id), nullptr);
        child.setProperty ("value", var (static_cast<double> (parameter->getValue())), nullptr);
        tree.addChild (child, -1);
    }

    return tree;
}

void AudioProcessorValueTreeState::replaceState (const ValueTree& state)
{
    if (! state.isValid())
        return;

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        const auto id = child.getProperty ("id", var()).asString();

        for (auto& parameter : parameters)
            if (parameter->getParameterID().id == id)
                parameter->setValue (static_cast<float> (static_cast<double> (
                    child.getProperty ("value", var (static_cast<double> (parameter->getValue()))))));
    }
}

} // namespace juce
