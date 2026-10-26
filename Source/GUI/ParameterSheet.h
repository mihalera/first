// =============================================================================
//  The parameter sheet: every parameter the plugin exposes, in one scrollable
//  list, built by foleys_gui_magic from the processor's own parameter tree.
//
//  The panel is 18 pages of hand-placed controls and 109 parameters. That is the
//  right shape for playing the machine and the wrong shape for finding one
//  control by name, for automating something that lives three pages away, or for
//  a screen reader walking the plugin. This sheet is the other view: grouped by
//  the parameter tree's own groups, ordered as the processor declares them, and
//  every control a real attachment rather than a copy of the value.
//
//  It is the first caller foleys_gui_magic has ever had here. The module was
//  linked and included with nothing using it (see docs/library-candidates.md);
//  the tree below is the declarative half it exists for, so the layout of this
//  surface is a ValueTree rather than another 200 lines of setBounds. The
//  stylesheet is the module's default, with the panel's palette laid over it,
//  so the sheet follows the theme without owning any geometry.
//
//  WHAT EACH ROW SAYS. Two things, and the sheet needed both:
//
//    * The CAPTION is the parameter's host name passed through xlat(), so a
//      Ukrainian panel lists Ukrainian names rather than the host's English
//      automation strings. The names are keyed sentences in the same tables the
//      tooltips use ("Input" -> "Вхід"), which is why they need no second list
//      here: the table that translates the panel translates the sheet.
//    * The EXPLANATION is the panel's own tooltip text for that parameter
//      (parameterTooltipText, PanelText.h), shown when the pointer rests on a
//      row. A sheet of bare names is a list of labels - "In EQ Order" and "DI
//      XFMR" are not self-explanatory - and the descriptions already existed
//      for the knobs, so the sheet reads the same table instead of inventing a
//      second vocabulary for the same controls.
//
//  The #if is not decoration: tests/editor/check_editor_juce.sh compiles every
//  header under Source/GUI against JUCE WITHOUT the CPM dependency trees on its
//  include path, exactly as it compiles the plugin without NAM Core. The sheet
//  defines a stub there, so that pass keeps compiling the shape of the plugin a
//  pruned checkout would have - and the editor asks `available` before it
//  offers the button, rather than opening a window onto nothing.
// =============================================================================
#pragma once

#include "GUI/PanelPaint.h"
#include "GUI/PanelText.h"
#include "GUI/PanelTranslations.h"

#if __has_include(<foleys_gui_magic/foleys_gui_magic.h>)
    #include <foleys_gui_magic/foleys_gui_magic.h>
    #define J37_HAS_PARAMETER_SHEET 1
#else
    #define J37_HAS_PARAMETER_SHEET 0
#endif

namespace j37panel
{
#if J37_HAS_PARAMETER_SHEET

/** A full-editor overlay listing every parameter, generated from the tree. */
class ParameterSheet final : public juce::Component
{
  public:
    static constexpr bool available = true;

    ParameterSheet (juce::AudioProcessor& processorToUse, const UiPalette& paletteToUse);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

  private:
    /** The scrolling surface itself: the foleys state, the builder and the
        canvas the builder lays its components out on.

        It is also the sheet's TooltipClient. The rows are foleys' own components
        (a SliderItem, a ComboBoxItem), so there is nowhere per-row to hang a
        tooltip without subclassing three of foleys' factories; answering as the
        PARENT is the same mechanism juce::TooltipWindow already uses, and it
        keeps the lookup in one place: the component under the pointer, its
        GuiItem, that item's parameter id, and the panel's description of it. */
    class Sheet final : public juce::Component, public juce::TooltipClient
    {
      public:
        Sheet (juce::AudioProcessor& processorToUse, const UiPalette& paletteToUse);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** The description of the parameter under the pointer, translated, or an
            empty string over the sheet's own furniture. */
        juce::String getTooltip() override;

      private:
        juce::ValueTree buildTree (juce::AudioProcessor& processorToUse);
        void appendGroup (juce::ValueTree& parent,
                          const juce::AudioProcessorParameterGroup& group);
        void appendParameter (juce::ValueTree& parent, juce::AudioProcessorParameter& parameter);

        static int countParameters (const juce::AudioProcessorParameterGroup& group);

        UiPalette palette;
        juce::Viewport viewport;
        juce::Component canvas;
        // The state must outlive the builder, and both must outlive the canvas's
        // children: the builder owns the GuiItems, the state owns the parameter
        // map and the attachments' source. Declaration order is destruction order
        // reversed, so this is also the order they are built in.
        std::unique_ptr<foleys::MagicProcessorState> state;
        std::unique_ptr<foleys::MagicGUIBuilder> builder;
        int parameterCount = 0;
        int groupCount = 0;
    };

    UiPalette palette;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::TextButton closeButton { "CLOSE" };
    std::unique_ptr<Sheet> sheet;
};

// =============================================================================
//  Implementation
// =============================================================================
inline ParameterSheet::ParameterSheet (juce::AudioProcessor& processorToUse,
                                       const UiPalette& paletteToUse)
    : palette (paletteToUse)
{
    setOpaque (true);
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);

    // The sheet is part of the panel, so its own three words follow the interface
    // language through the same table: these are keyed entries, because a title
    // has no English sentence to key on (see the note above xlat).
    titleLabel.setText (xlat ("PARAMETERS").toUpperCase(), juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, palette.accent);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText (xlat ("every control the plugin exposes, grouped as the processor "
                                "declares it - rest the pointer on a row for what it does"),
                           juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    subtitleLabel.setColour (juce::Label::textColourId, palette.secondary);
    addAndMakeVisible (subtitleLabel);

    closeButton.setColour (juce::TextButton::buttonColourId, palette.raised);
    closeButton.setColour (juce::TextButton::textColourOffId, palette.text);
    closeButton.setColour (juce::TextButton::buttonOnColourId, palette.raised.darker (0.15f));
    closeButton.setButtonText (xlat ("CLOSE"));
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    sheet = std::make_unique<Sheet> (processorToUse, paletteToUse);
    addAndMakeVisible (*sheet);

    setVisible (false);
}

inline void ParameterSheet::paint (juce::Graphics& g)
{
    g.fillAll (palette.background);

    // A hairline under the header, the same divider language the panel uses
    // between its bands.
    g.setColour (palette.border.withAlpha (0.55f));
    g.fillRect (12, 46, getWidth() - 24, 1);
}

inline void ParameterSheet::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto header = area.removeFromTop (46);
    titleLabel.setBounds (header.removeFromTop (24));
    subtitleLabel.setBounds (header.removeFromTop (16));
    closeButton.setBounds (getWidth() - 12 - 84, 10, 84, 26);

    area.removeFromTop (6);
    if (sheet != nullptr)
        sheet->setBounds (area);
}

inline bool ParameterSheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }

    return false;
}

inline void ParameterSheet::open()
{
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    resized();
}

inline void ParameterSheet::close()
{
    setVisible (false);
}

// =============================================================================
//  The sheet's content
// =============================================================================
inline ParameterSheet::Sheet::Sheet (juce::AudioProcessor& processorToUse,
                                     const UiPalette& paletteToUse)
    : palette (paletteToUse)
{
    state = std::make_unique<foleys::MagicProcessorState> (processorToUse);
    // Built before the GUI tree is set: the builder takes the parameter map when
    // it creates the attachments, and a map built on first use would miss the
    // ones already asked for.
    state->updateParameterMap();
    state->setGuiValueTree (buildTree (processorToUse));

    builder = std::make_unique<foleys::MagicGUIBuilder> (*state);
    builder->registerJUCEFactories();
    builder->registerJUCELookAndFeels();
    builder->createGUI (canvas);

    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

inline juce::String ParameterSheet::Sheet::getTooltip()
{
    // The component under the pointer, then its nearest GuiItem ancestor. Using
    // the desktop's own answer rather than arithmetic on getMouseXYRelative()
    // keeps this correct for a row that is half scrolled out of the viewport.
    auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();

    while (under != nullptr && under != this && dynamic_cast<foleys::GuiItem*> (under) == nullptr)
        under = under->getParentComponent();

    // Not over the sheet at all, or over the sheet's own empty background: no tip
    // rather than the last row's, which is what a cached answer would show.
    if (under == nullptr || under == this || ! isParentOf (under))
        return {};

    auto* item = dynamic_cast<foleys::GuiItem*> (under);
    if (item == nullptr)
        return {};

    const auto id = item->getControlledParameterID ({});
    if (id.isEmpty())
        return {};

    return xlat (parameterTooltipText (id));
}

inline void ParameterSheet::Sheet::paint (juce::Graphics& g)
{
    g.fillAll (palette.background);

    if (parameterCount == 0)
    {
        g.setColour (palette.secondary);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText ("this build exposes no parameters", getLocalBounds(),
                          juce::Justification::centred, 1);
    }
}

inline void ParameterSheet::Sheet::resized()
{
    viewport.setBounds (getLocalBounds());

    // The sheet's height is the estimate the flex column is then distributed
    // over: each parameter row is one share, each group's caption and border
    // another. It is an estimate rather than a measurement because the layout
    // belongs to foleys - the number here only decides how tall the scroll
    // surface is, and a row a few pixels taller than planned is a scroll, not a
    // clipped control.
    const auto rows = juce::jmax (1, parameterCount) * 74;
    const auto caption = groupCount * 34;
    const auto wanted = 18 + rows + caption;

    const auto scrolls = wanted > viewport.getHeight();
    const auto width = juce::jmax (320, viewport.getWidth()
                                             - (scrolls ? viewport.getScrollBarThickness() : 0));

    canvas.setSize (width, juce::jmax (viewport.getHeight(), wanted));

    if (builder != nullptr)
        builder->updateLayout (canvas.getLocalBounds());
}

inline int ParameterSheet::Sheet::countParameters (const juce::AudioProcessorParameterGroup& group)
{
    auto count = group.getParameters (false).size();

    for (const auto* sub : group.getSubgroups (false))
        if (sub != nullptr)
            count += countParameters (*sub);

    return count;
}

inline juce::ValueTree ParameterSheet::Sheet::buildTree (juce::AudioProcessor& processorToUse)
{
    juce::ValueTree root (foleys::IDs::view);
    root.setProperty (foleys::IDs::id, foleys::IDs::root, nullptr);
    root.setProperty (foleys::IDs::styleClass, "nomargin", nullptr);
    root.setProperty (foleys::IDs::flexDirection, foleys::IDs::flexDirColumn, nullptr);
    root.setProperty (foleys::IDs::padding, 6, nullptr);
    root.setProperty (foleys::IDs::backgroundColour,
                      palette.background.toDisplayString (false), nullptr);

    // The colour names are the ones SliderItem declares in its own
    // setColourTranslation() - the stylesheet resolves them per node and
    // inherits them from here, so every slider the builder makes carries the
    // panel's ink without a factory or a LookAndFeel of our own.
    root.setProperty (foleys::IDs::captionColour, palette.secondary.toDisplayString (false), nullptr);
    root.setProperty (juce::Identifier ("slider-track"), palette.accent.toDisplayString (false), nullptr);
    root.setProperty (juce::Identifier ("slider-background"), palette.raised.toDisplayString (false), nullptr);
    root.setProperty (juce::Identifier ("slider-thumb"), palette.text.toDisplayString (false), nullptr);
    root.setProperty (juce::Identifier ("rotary-fill"), palette.accent.toDisplayString (false), nullptr);
    root.setProperty (juce::Identifier ("rotary-outline"), palette.border.toDisplayString (false), nullptr);
    root.setProperty (juce::Identifier ("slider-text"), palette.text.toDisplayString (false), nullptr);
    root.setProperty (juce::Identifier ("slider-text-background"),
                      palette.readout.toDisplayString (false), nullptr);

    const auto& tree = processorToUse.getParameterTree();

    for (const auto* group : tree.getSubgroups (false))
        if (group != nullptr)
            appendGroup (root, *group);

    for (auto* parameter : tree.getParameters (false))
        if (parameter != nullptr)
            appendParameter (root, *parameter);

    juce::ValueTree magic (foleys::IDs::magic);
    magic.appendChild (root, nullptr);
    return magic;
}

inline void ParameterSheet::Sheet::appendGroup (juce::ValueTree& parent,
                                                const juce::AudioProcessorParameterGroup& group)
{
    const auto count = countParameters (group);
    if (count == 0)
        return;

    juce::ValueTree view (foleys::IDs::view);
    view.setProperty (foleys::IDs::styleClass, "group", nullptr);
    view.setProperty (foleys::IDs::caption, group.getName(), nullptr);
    view.setProperty (foleys::IDs::flexDirection, foleys::IDs::flexDirColumn, nullptr);
    // One share per parameter: a group of twelve is not squeezed into the space
    // a group of two gets. This is the whole of the layout arithmetic.
    view.setProperty (foleys::IDs::flexGrow, count, nullptr);
    view.setProperty (foleys::IDs::backgroundColour, palette.panel.toDisplayString (false), nullptr);
    view.setProperty (foleys::IDs::borderColour, palette.border.toDisplayString (false), nullptr);

    for (const auto* sub : group.getSubgroups (false))
        if (sub != nullptr)
            appendGroup (view, *sub);

    for (auto* parameter : group.getParameters (false))
        if (parameter != nullptr)
            appendParameter (view, *parameter);

    parent.appendChild (view, nullptr);
    ++groupCount;
}

inline void ParameterSheet::Sheet::appendParameter (juce::ValueTree& parent,
                                                    juce::AudioProcessorParameter& parameter)
{
    const auto* withID = dynamic_cast<const juce::AudioProcessorParameterWithID*> (&parameter);
    if (withID == nullptr)
        return;

    // The three shapes foleys can bind straight to a parameter. Choice and bool
    // are not sliders with a range: a Slider on a choice parameter would show
    // its index and land between the choices.
    juce::ValueTree item (dynamic_cast<juce::AudioParameterChoice*> (&parameter) != nullptr
                              ? foleys::IDs::comboBox
                          : dynamic_cast<juce::AudioParameterBool*> (&parameter) != nullptr
                              ? foleys::IDs::toggleButton
                              : foleys::IDs::slider);

    // Through xlat(): the caption is a row of the sheet, not the host's
    // automation list, so it follows the interface language. A table without an
    // entry for the name returns the name, which is the same English the host
    // shows - the honest fallback, not a raw key.
    item.setProperty (foleys::IDs::caption, xlat (parameter.getName (64)), nullptr);
    item.setProperty (foleys::IDs::parameter, withID->paramID, nullptr);

    parent.appendChild (item, nullptr);
    ++parameterCount;
}

#else // ! J37_HAS_PARAMETER_SHEET

/** A checkout without the foleys tree has no sheet. The editor asks `available`
    before it offers one, so this type is only the shape its pointer needs. */
class ParameterSheet final : public juce::Component
{
  public:
    static constexpr bool available = false;

    ParameterSheet (juce::AudioProcessor&, const UiPalette&) {}
    void open() {}
    void close() {}
    [[nodiscard]] bool isOpen() const noexcept { return false; }
};

#endif
} // namespace j37panel
