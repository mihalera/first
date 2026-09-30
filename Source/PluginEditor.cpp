#include "PluginProcessor.h"

// The generated resource accessors: the factory presets and the translation
// tables, both compiled into the binary by CMakeLists.txt.
#include <BinaryData.h>
#include "PluginEditor.h"

#include <iostream>
#include <utility>
#include <vector>

#if JUCE_DEBUG
 #include <melatonin_inspector/melatonin_inspector.h>
#endif

//==============================================================================
//  Interface sounds.
//
//  The engine is a four-voice tick synthesiser with its own audio device. The
//  design constraints, in order of importance:
//
//    1. It must NEVER reach the plugin's audio output. It has its own device,
//       so a click cannot be recorded, cannot appear in the DAW's meters and
//       cannot be bounced into a render. This is the whole reason it is a
//       separate device rather than a voice inside the plugin's engine.
//
//    2. It must never fail the editor. If no device can be opened - a headless
//       host, an exclusive-mode driver, a machine with no output at all - the
//       engine simply stays silent and the panel works exactly as before. The
//       open is attempted once and never retried.
//
//    3. It must cost nothing. The render is a sine, a noise burst and a
//       two-stage envelope; the buffer is 256 samples at the device's rate.
//==============================================================================
FirstAudioProcessorEditor::UiSoundEngine::UiSoundEngine()
{
    // An output-only device, opened lazily and never retried: a host that has no
    // spare output, a machine whose only device is held exclusively by the DAW,
    // or a headless CI runner must all end up with a silent engine and a working
    // panel rather than an error or a hang.
    const auto error = deviceManager.initialise (0, 2, nullptr, true, {}, nullptr);
    deviceOpen = error.isEmpty();

    if (deviceOpen)
        deviceManager.addAudioCallback (this);
}

FirstAudioProcessorEditor::UiSoundEngine::~UiSoundEngine()
{
    if (deviceOpen)
        deviceManager.removeAudioCallback (this);
}

void FirstAudioProcessorEditor::UiSoundEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    phase = 0.0f;
    envelope = 0.0f;
    noiseState = 0.37f;
}

void FirstAudioProcessorEditor::UiSoundEngine::audioDeviceStopped()
{
    envelope = 0.0f;
}

void FirstAudioProcessorEditor::UiSoundEngine::trigger (Voice voice) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    // The four voices, each a different synthesis rather than the same beep at a
    // different pitch. `brightness` (the machine's own activity) lifts all four
    // slightly, so a busy machine sounds slightly higher and tighter.
    //
    // The decay figure is the per-sample envelope multiplier at the DEVICE's
    // rate; because it is re-derived from the rate in the render it is stored
    // here as a time constant instead, which keeps the four voices the same
    // length at every device rate.
    const auto lift = brightness.load (std::memory_order_relaxed);

    switch (voice)
    {
        case Voice::click:
            // A switch: short, dry, bright. Almost all noise, very fast decay.
            pendingAmplitude.store (0.20f, std::memory_order_relaxed);
            pendingPitch.store (1900.0f + lift * 700.0f, std::memory_order_relaxed);
            pendingDecay.store (0.30f, std::memory_order_relaxed);   // ms of tail
            pendingNoiseMix.store (0.85f, std::memory_order_relaxed);
            break;

        case Voice::detent:
            // A knob crossing a step: softer, lower, mostly tone.
            pendingAmplitude.store (0.10f, std::memory_order_relaxed);
            pendingPitch.store (900.0f + lift * 400.0f, std::memory_order_relaxed);
            pendingDecay.store (0.55f, std::memory_order_relaxed);
            pendingNoiseMix.store (0.30f, std::memory_order_relaxed);
            break;

        case Voice::press:
            // A key going down: a low, dry thump.
            pendingAmplitude.store (0.16f, std::memory_order_relaxed);
            pendingPitch.store (420.0f + lift * 120.0f, std::memory_order_relaxed);
            pendingDecay.store (1.40f, std::memory_order_relaxed);
            pendingNoiseMix.store (0.45f, std::memory_order_relaxed);
            break;

        case Voice::release:
            // ...and coming back up: a touch higher, so a key pair reads as two
            // events rather than one blip when pressed quickly.
            pendingAmplitude.store (0.12f, std::memory_order_relaxed);
            pendingPitch.store (620.0f + lift * 160.0f, std::memory_order_relaxed);
            pendingDecay.store (0.90f, std::memory_order_relaxed);
            pendingNoiseMix.store (0.35f, std::memory_order_relaxed);
            break;
    }
}

void FirstAudioProcessorEditor::UiSoundEngine::audioDeviceIOCallbackWithContext (
    const float* const*, int, float* const* outputChannelData, int numOutputChannels,
    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    if (outputChannelData == nullptr || numOutputChannels <= 0)
        return;

    // A new trigger takes over whatever was ringing, which is what a real panel
    // does: the envelope is re-armed rather than layered, so a fast series of
    // clicks sounds like a switch being flicked rather than a chord.
    const auto amplitude = pendingAmplitude.exchange (0.0f, std::memory_order_relaxed);
    if (amplitude > 0.0f)
    {
        envelope = amplitude;
        const auto pitchHz = pendingPitch.load (std::memory_order_relaxed);
        phase = 0.0f;
        (void) pitchHz;   // read below, through the increment
        decay = pendingDecay.load (std::memory_order_relaxed);
        noiseMix = pendingNoiseMix.load (std::memory_order_relaxed);
    }

    if (envelope < 1.0e-5f)
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
        return;
    }

    // The pitch is read here rather than in trigger() because it depends on the
    // DEVICE's rate, which trigger() (on the message thread) does not know.
    const auto pitchHz = pendingPitch.load (std::memory_order_relaxed);
    const auto phaseIncrement = static_cast<float> (juce::MathConstants<double>::twoPi
                                                        * pitchHz / deviceRate);

    // The envelope multiplier per sample, derived from the voice's tail in
    // MILLISECONDS so the four sounds keep their designed length whatever rate
    // the device happens to run at.
    const auto tailSamples = juce::jmax (1.0f, static_cast<float> (deviceRate) * decay * 0.001f);
    const auto decayPerSample = std::exp (-1.0f / tailSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto tone = std::sin (phase);
        phase += phaseIncrement;
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        // The noise burst: a fast, cheap LCG so the tick has a bright edge and
        // does not read as a pure sine beep. It is filtered slightly by mixing
        // with the previous value, which takes the harshest digital edge off.
        noiseState = noiseState * 1.27f + 0.31f;
        if (noiseState > 1.0f) noiseState -= 2.0f;
        const auto noise = noiseState;

        const auto voiceOut = (tone * (1.0f - noiseMix) + noise * noiseMix) * envelope;

        for (int channel = 0; channel < numOutputChannels; ++channel)
            outputChannelData[channel][sample] = voiceOut;

        envelope *= decayPerSample;
    }
}

namespace
{
    // Symmetric decibel range shared by the input and output trims, matching the
    // processor's parameter layout exactly.
    constexpr double minStageDb = -32.0;
    constexpr double maxStageDb = 32.0;

    struct UiPalette
    {
        juce::Colour background;
        juce::Colour panel;
        juce::Colour raised;
        juce::Colour border;
        juce::Colour accent;
        juce::Colour text;
        juce::Colour secondary;
        juce::Colour knobFace;
        juce::Colour knobHighlight;
        juce::Colour knobEdge;
        juce::Colour readout;
        juce::Colour gaugeFace;
        juce::Colour gaugeInk;
        juce::Colour needle;
        juce::Colour status;
    };

    // NOTE: this struct is aggregate-initialised positionally below, so the field order here
    // and the order of the colour literals in each palette must stay in step. `card` used to
    // sit third and was never read, which meant every colour after it was one field adrift of
    // its name; it has been removed and the literals below follow this order exactly.
    // If you add a field, add it at the end and append its literal to both palettes.

    const UiPalette ivoryPalette {
        juce::Colour::fromRGB (197, 184, 157),   // background
        juce::Colour::fromRGB (222, 210, 187),   // panel
        juce::Colour::fromRGB (231, 220, 197),   // raised
        juce::Colour::fromRGB (111, 97, 73),     // border
        juce::Colour::fromRGB (145, 96, 42),     // accent
        juce::Colour::fromRGB (45, 38, 29),      // text
        juce::Colour::fromRGB (102, 91, 72),     // secondary
        juce::Colour::fromRGB (179, 162, 130),   // knobFace
        juce::Colour::fromRGB (217, 203, 176),   // knobHighlight
        juce::Colour::fromRGB (93, 78, 55),      // knobEdge
        juce::Colour::fromRGB (245, 238, 222),   // readout
        juce::Colour::fromRGB (239, 229, 203),   // gaugeFace
        juce::Colour::fromRGB (49, 43, 33),      // gaugeInk
        juce::Colour::fromRGB (132, 47, 37),     // needle
        juce::Colour::fromRGB (62, 111, 72)      // status
    };

    const UiPalette charcoalPalette {
        juce::Colour::fromRGB (28, 25, 21),      // background
        juce::Colour::fromRGB (54, 46, 35),      // panel
        juce::Colour::fromRGB (84, 70, 50),      // raised
        juce::Colour::fromRGB (144, 117, 72),    // border
        juce::Colour::fromRGB (211, 166, 83),    // accent
        juce::Colour::fromRGB (244, 231, 204),   // text
        juce::Colour::fromRGB (190, 173, 140),   // secondary
        juce::Colour::fromRGB (89, 72, 51),      // knobFace
        juce::Colour::fromRGB (126, 105, 73),    // knobHighlight
        juce::Colour::fromRGB (209, 172, 103),   // knobEdge
        juce::Colour::fromRGB (35, 30, 24),      // readout
        juce::Colour::fromRGB (226, 210, 173),   // gaugeFace
        juce::Colour::fromRGB (48, 39, 28),      // gaugeInk
        juce::Colour::fromRGB (132, 47, 37),     // needle
        juce::Colour::fromRGB (122, 174, 128)    // status
    };

    // ------------------------------------------------------------------
    //  The METAL theme: brushed steel rather than wood or paper.
    //
    //  A third theme rather than a tint of the other two, because it says a
    //  different thing about the machine: ivory is a piece of studio furniture
    //  and charcoal is an old console, while metal is a rack unit - the
    //  19-inch, grey, slightly cold piece of gear that lives in a machine room.
    //
    //  The palette is built around that: the chassis is a NEUTRAL grey rather
    //  than a warm one, the accent is a cool cyan rather than gold or amber,
    //  and the "metal" itself comes from the contrast between the raised
    //  surfaces and the recesses rather than from a warm colour. Every colour
    //  is deliberately desaturated - a metal panel gets its depth from its
    //  lighting, not from its pigment - which is also what keeps the accent
    //  reading as an indicator lamp rather than as a decoration.
    //
    //  It is DARK in the sense the code means - it draws dark text on light
    //  and light text on dark - so the two flags below keep it on the same
    //  code paths as charcoal and only the colours differ.
    // ------------------------------------------------------------------
    //  The METAL palette, second pass. The first pass shared its whole value
    //  range with charcoal (six of fifteen colours within a few RGB points),
    //  which is what made metal read as the dark theme's demo. This one is built
    //  from what a rack panel actually is: a near-black graphite chassis whose
    //  depth comes from brushed speculars rather than warmth, machined
    //  aluminium lettering plates, and the one saturated thing on the panel -
    //  the signal-green indicator lamp family. The gauge face is the anti-panel:
    //  pale, where the chassis is black, which is the exact inversion the
    //  hardware this imitates uses (VU ballistics on a lit window in a dark face).
    const UiPalette metalPalette {
        juce::Colour::fromRGB (16, 17, 19),      // background - graphite chassis
        juce::Colour::fromRGB (37, 39, 43),      // panel - brushed dark steel
        juce::Colour::fromRGB (58, 62, 68),      // raised - machined plate
        juce::Colour::fromRGB (105, 112, 121),   // border - bright machined edge
        juce::Colour::fromRGB (64, 224, 148),    // accent - signal green, the panel's one saturated voice
        juce::Colour::fromRGB (232, 236, 240),   // text - etched white
        juce::Colour::fromRGB (148, 156, 164),   // secondary - engraved grey
        juce::Colour::fromRGB (46, 49, 54),      // knobFace
        juce::Colour::fromRGB (96, 102, 110),    // knobHighlight - the knurl's sheen
        juce::Colour::fromRGB (196, 202, 210),   // knobEdge - bright machined ring
        juce::Colour::fromRGB (12, 13, 15),      // readout - dead black window
        juce::Colour::fromRGB (214, 220, 226),   // gaugeFace - the pale ballistic window
        juce::Colour::fromRGB (24, 26, 29),      // gaugeInk
        juce::Colour::fromRGB (255, 120, 60),    // needle - hot orange, distinct from the green
        juce::Colour::fromRGB (110, 220, 160)    // status - the same green family, dimmer
    };

    // The live choice, kept in step with the editor's by applyTheme(). It lives
    // in the anonymous namespace beside the palettes so both lookups share it.
    J37LookAndFeel::ThemeChoice liveThemeChoice = J37LookAndFeel::ThemeChoice::ivory;

    // The three-theme lookup. `paletteFor(bool)` below is kept because it is
    // what every paint routine already calls, and the two dark themes share
    // every code path - this is the one place the two of them are told apart.
    // The parameter is J37LookAndFeel::ThemeChoice itself: a second, local enum
    // with the same enumerators compiled on its own and then could not accept
    // the look-and-feel's member (two distinct types), which is what broke
    // drawToggleButton. One enum, declared once in the header.
    //
    // This one is DEFINED FIRST, and the order is load-bearing: both of these
    // are free functions in the anonymous namespace, so each may only use what
    // is declared above it. Written the other way round, paletteFor could not
    // see the lookup or the live choice, and the failure reads as if the
    // functions did not exist at all.
    const UiPalette& paletteForTheme (J37LookAndFeel::ThemeChoice theme)
    {
        switch (theme)
        {
            case J37LookAndFeel::ThemeChoice::metal:    return metalPalette;
            case J37LookAndFeel::ThemeChoice::charcoal: return charcoalPalette;
            case J37LookAndFeel::ThemeChoice::ivory:
            default:                                    return ivoryPalette;
        }
    }

    // The bool overload is the single funnel every paint routine and every
    // styling call goes through. It used to map "dark" to charcoal outright,
    // which is exactly why METAL read as a demo of the dark theme: the panel's
    // ~60 dark call sites never saw the metal palette at all. The funnel now
    // reads the live three-way choice, so a theme switch changes every surface
    // that draws through here - and paint() (whose only information IS the bool)
    // lands on the right palette for the first time.
    //
    // No `const` on this one: it is a free function, not a member, and a cv-
    // qualifier on a non-member function is a hard error ("cannot have
    // cv-qualifier") rather than something to be ignored.
    const UiPalette& paletteFor (bool darkTheme) noexcept
    {
        juce::ignoreUnused (darkTheme);
        return paletteForTheme (liveThemeChoice);
    }

    // Shading for the cylindrical switch bodies (drawToggleButton, both look
    // and feels). The switch is a cylinder lying along its own length, lit
    // from above: a point on the projected face has its surface normal tilted
    // by the cross-section angle a, so the light it catches is cos(a) - the
    // brightness peaks just ABOVE the centreline (where the curvature faces
    // the lamp), falls to the rim on top, and dies into shadow underneath.
    // That profile is painted twice: once as the barrel's gradient, once as a
    // thin specular line riding the same falloff - which is what turns a
    // filled shape into something that reads as machined metal.
    void fillCylinderBarrel (juce::Graphics& g, const juce::Rectangle<float>& barrel,
                             juce::Colour face, juce::Colour edge)
    {
        if (barrel.getWidth() < 2.0f || barrel.getHeight() < 2.0f)
            return;

        // The barrel: cosine shading down the height, one-sided because the
        // light sits above the machine. The silhouette is the full capsule
        // (radius = half the height), which IS the cylinder's projection.
        juce::ColourGradient shading (
            edge, barrel.getCentreX(), barrel.getY(),
            edge.darker (0.35f), barrel.getCentreX(), barrel.getBottom(), false);
        shading.addColour (0.30, face.brighter (0.30f));
        shading.addColour (0.62, face);
        g.setGradientFill (shading);
        g.fillRoundedRectangle (barrel, barrel.getHeight() * 0.5f);

        // The specular: a thin bright line along the barrel's length, riding
        // above the centreline. It is the single strongest cue that the
        // surface is curved rather than flat.
        const auto streak = juce::Rectangle<float> (
            barrel.getX() + barrel.getWidth() * 0.06f,
            barrel.getY() + barrel.getHeight() * 0.12f,
            barrel.getWidth() * 0.88f,
            juce::jmin (barrel.getHeight() * 0.16f, 3.5f));
        juce::ColourGradient specular (
            face.brighter (0.60f).withAlpha (0.65f),
            streak.getX(), streak.getY(),
            face.brighter (0.60f).withAlpha (0.0f),
            streak.getX(), streak.getBottom(), false);
        g.setGradientFill (specular);
        g.fillRoundedRectangle (streak, streak.getHeight() * 0.5f);
    }

    // The three tabs the knob grid is split across. Each entry names a tab and lists the
    // controlIds indices it holds, in the order they are drawn: left to right, then down.
    //
    // The order is SIGNAL FLOW, not the order the parameters happen to be declared in,
    // and that is the whole point of the table. It used to group controls by where they
    // sat in one five-row block, so MACHINE ended with SUBFUND (a low-frequency weight,
    // read as if it were a modulation control) and the head/transport tab opened with
    // PRESENCE before WOW and FLUTTER, which put the playback EQ in front of the tape
    // speed controls it acts on. Now:
    //
    //   MACHINE           what goes in, the machine's tone, what comes out
    //   SATURATION CORE   the level into the saturator, then everything that bends it
    //   HEAD / TRANSPORT  the head, then the transport, then echo, then hiss
    //
    // Each tab reads in the order its own name states, and the three of them are
    // consecutive slices of the same chain, so scanning left to right across the panel
    // walks the signal from the input trim to the tape hiss.
    //
    // This table - not the order of controlIds - decides what the grid shows, and
    // setCurrentTab() static_asserts it against controlCount, so a control no tab lists
    // cannot slip through to sit in the panel where it can never be seen or reached.
    struct TabSpec
    {
        const char* name;
        const char* hint;
        std::size_t count;
        std::size_t controls[16];
    };

    // Six pages, in signal order, because the panel now carries thirty-one knobs and
    // one surface cannot hold them legibly. The split follows the CHAIN rather than an
    // arbitrary grouping, so walking the tabs left to right walks the signal:
    //
    //   MACHINE   what goes in, how the machine colours it, what comes out
    //   DRIVE     the gain stages in front of the tape, then everything that bends
    //   TAPE      the head, the medium's condition and the transport's
    //   SPACE     the two time-based stages, delay and reverb
    //   VINYL     the record-playing end of the chain
    //
    // Index map, for reading the table below:
    //   0 input  1 drive  2 bias  3 tone  4 character  5 wow  6 flutter  7 mix
    //   8 output  9 stereo_width  10 blend  11 shape  12 amp_bias  13 sag
    //   14 presence  15 cabinet  16 delay_time  17 delay_feedback  18 st_offset
    //   19 noise  20 subfund  21 preamp  22 distortion  23 flux  24 wear
    //   25 mechanics  26 reverb  27 reverb_size  28 vinyl  29 vinyl_crackle
    //   30 vinyl_rumble  31 noise_lvl  32 st_link
    //   33 dust  34 scratch  35 warp  36 electrical  37 clicks
    //   38 in_low  39 in_mid  40 in_high   41 out_low  42 out_mid  43 out_high
    //   44 di  45 di_load  46 di_transformer
    //   47 in_hp_freq  48 in_lp_freq  49 in_eq_q
    //   50 out_hp_freq  51 out_lp_freq  52 out_eq_q  53 delay_pingpong
    //
    // The map above is the controlIds list's own order (the one place the indices
    // are defined), kept in step with it by hand and checked by the static_assert
    // further down that every index is inside the array. It was written when the
    // panel carried thirty-seven knobs and stopped there, while the list below
    // the table has since grown to fifty-four - so four of the tab rows above
    // referred to indices that had silently been re-used for the EQ and DI
    // controls. The indices are now all listed.
    //
    // The deck's non-knob switches follow the tabs too: GL and OVERSAMPLING show
    // on SETTINGS, and the delay TYPE / RATE / SYNC trio shows on SPACE. They are
    // not knobs - the grid below cannot place them - so their visibility is
    // managed in setCurrentTab beside the knobs'.
    constexpr std::array<TabSpec, 10> tabSpecs { {
        //  input, tone (BRIGHT), character (TONE), mix, stereo_width, output
        { "MACHINE", "What goes in, how the machine colours it, and what comes out.",
                     7, { 0, 3, 4, 7, 9, 8, 32 } },
        //  preamp, distortion, drive, bias, blend, shape, amp_bias, sag, subfund
        { "DRIVE", "The gain stages in front of the tape, then everything that bends "
                   "the signal.",
                     12, { 44, 45, 46, 21, 22, 1, 2, 10, 11, 12, 13, 20 } },
        //  flux, cabinet, presence. The vinyl stage's three voicing selectors -
        //  GENERATION / TURNTABLE / CARTRIDGE - moved here from the old VINYL
        //  page as well; they are COMBO BOXES rather than knobs (the grid only
        //  places knobs), so like GL and OVERSAMPLING they are tab members
        //  whose visibility setCurrentTab asserts and resized() lays out.
        { "CHARACTER", "The head, the medium's tone, and how the record was made, "
                       "what plays it and what reads it: GENERATION, TURNTABLE and "
                       "CARTRIDGE re-voice the whole vinyl stage, so they live with "
                       "the rest of the machine's voicing rather than beside its "
                       "faults.",
                     3, { 23, 15, 14 } },
        //  noise, noise_lvl, wow, flutter, wear, mechanics - the MACHINE's own
        //  departures from a clean signal, which is what noise means in the
        //  widest sense. Wow and flutter are the transport's noise, wear and
        //  mechanics the medium's and the mechanism's, and they share the
        //  transport gate with the hiss. The record's five FAULTS are here too
        //  - DUST the surface's fine texture, SCRATCH a wound crossed once per
        //  revolution, WARP the level breathing at the platter rate, ELECTRICAL
        //  the cartridge's earthing, CLICKS the pressing's sharp faults - so
        //  every knob on this page is a way of making the signal LESS clean.
        { "NOISE", "Everything that departs from a clean signal. NOISE MIX sets how "
                   "much of it is in the output and NOISE LVL how loud the sources "
                   "run; WOW and FLUTTER are the transport's noise, WEAR and "
                   "MECHANICS the medium's and the mechanism's, and the record's "
                   "five FAULTS are here too - DUST the surface's fine texture, "
                   "SCRATCH a wound crossed once per revolution, WARP the level "
                   "breathing at the platter rate, ELECTRICAL the cartridge's "
                   "earthing, CLICKS the pressing's sharp faults.",
                     11, { 19, 31, 5, 6, 24, 25, 33, 34, 35, 36, 37 } },
        //  delay_time, delay_feedback, st_offset, reverb, reverb_size
        { "SPACE", "The two time-based stages: the second head, then the room. The "
                   "deck's TYPE / SYNC DELAY / RATE switches belong to the second "
                   "head, so they show on this tab.",
                     6, { 16, 17, 18, 53, 26, 27 } },
        //  transient_attack (54), transient_sustain (55), transient_mix (56),
        //  neural_mix (57). The DYNAMICS page: the two stages that act on the
        //  finished signal's envelope and on a learned model rather than on the
        //  waveform. They are together because both sit AFTER the machine and both
        //  are "how the sound moves" rather than "how the sound is bent" - the
        //  transient shaper changes the envelope without adding a harmonic, and
        //  the neural stage adds a learned saturation on top of the hand-written
        //  one. NEURAL is a MIX because the model itself is loaded from a file,
        //  not chosen with a knob.
        { "DYN", "The two stages that shape the FINISHED signal. ATK sharpens "
                    "(positive) or softens (negative) the attack of each event; "
                    "SUS lengthens (positive) or shortens (negative) what follows "
                    "the attack, its body and ring; TR MIX is how much of the "
                    "shaped signal reaches the output. NEURAL is the wet/dry "
                    "position of an optional learned model loaded from a file - "
                    "at 0, or with no model loaded, it is transparent.",
                     4, { 54, 55, 56, 57 } },
        //  The record's five FAULTS moved onto NOISE (each is a mechanism of noise:
        //  surface texture, a repeating wound, the platter's warp, the cartridge's
        //  earthing, the pressing's clicks), so this page carries the stage's mix
        //  and its two continuous surfaces. The three selectors at the top of the
        //  old page - GENERATION / TURNTABLE / CARTRIDGE - moved to CHARACTER,
        //  where the rest of the machine's voicing lives.
        //
        //  These three are here and NOT also on NOISE: they are the record's
        //  CHARACTER (how much of the stage is in the output, and the two
        //  continuous surfaces), while the faults are noise. Listing them on
        //  both pages would put the same knob on two tabs, and the grid can only
        //  place one of them.
        { "VINYL", "The record-playing stage: VINYL MIX is how much of it is in the "
                    "output, CRACKLE the surface's granular texture and RUMBLE the "
                    "platter's own low thump. The record's five FAULTS - DUST, "
                    "SCRATCH, WARP, ELECTRICAL, CLICKS - live on the NOISE tab, "
                    "next to the rest of what departs from a clean signal; the "
                    "GENERATION / TURNTABLE / CARTRIDGE selectors that change how "
                    "all of them sound are on CHARACTER.",
                     3, { 28, 29, 30 } },
        //  in_low, in_mid, in_high - the input equaliser. Its own page because
        //  it is a different DECISION from the output EQ: this one changes what
        //  the machine hears, so it changes what the machine does.
        { "IN EQ", "The input equaliser, in front of the machine. What it shapes "
                    "is what the tape HEARS, so lifting the low end here drives the "
                    "saturation curve and the glue compressors harder - it changes "
                    "the character of the processing, not merely the balance. This "
                    "is the EQ you use to feed the machine what it wants. LOW is a "
                    "shelf at 200 Hz, MID a bell at 1 kHz, HIGH a shelf above "
                    "4 kHz; all three are transparent at 0 dB.",
                     6, { 38, 39, 40, 47, 48, 49 } },
        //  out_low, out_mid, out_high - the output equaliser.
        { "OUT EQ", "The output equaliser, after the machine and before the "
                     "output trim. Nothing downstream responds to what it does, so "
                     "it corrects the RESULT rather than the input - the neutral, "
                     "predictable EQ you use to place the finished sound. Same three "
                     "bands as the input EQ, same transparency at 0 dB.",
                     6, { 41, 42, 43, 50, 51, 52 } },
        //  No knobs of its own: SETTINGS is where the three engine-level switches
        //  live - GL, OVERSAMPLING and the interface sounds - shown by
        //  setCurrentTab, not by the grid.
        { "SETTINGS", "The engine-level switches. OVERSAMPLING sets the internal "
                      "rate the tape engine runs at, GL turns the GPU-accelerated "
                      "panel rendering on and off, and UI SOUNDS turns the panel's "
                      "own interface clicks on and off - those never reach the "
                      "audio output, they play on a device of their own.",
                     0, {} }
    } };

    // Where the divider under the knob-grid heading sits, in pixels from the top of the
    // panel. The tab bar lives between the heading and this line, so paint() and
    // resized() both read it from here: it used to be a literal 42 px in paint(), which
    // is precisely where the first tab button now is.
    constexpr int controlsDividerOffset = 68;

    // Does control `index` belong to `tab`?
    bool controlIsInTab (std::size_t index, int tab)
    {
        const auto& spec = tabSpecs[static_cast<std::size_t> (tab)];

        for (std::size_t i = 0; i < spec.count; ++i)
            if (spec.controls[i] == index)
                return true;

        return false;
    }

    // Compile-time proof that the tabs partition the grid: every control index appears
    // in exactly one tab, so no knob is orphaned (invisible and unreachable) or shown
    // twice, and none of them is silently dropped.
    // The tab count is written once, here, and the static_assert that guards coverage
// reads it from the same constant - so adding a page cannot leave this behind.
constexpr std::size_t numTabPages = 10;

// The control count is a TEMPLATE parameter, not an argument, and that is the
// whole fix. It used to be a literal 54 inside the function - the current
// control count, written down a second time. The day a control was added the
// array was still 54 long, so a tab row naming index 54 or 55 would have
// returned false for the wrong reason, and growing the array would have meant
// remembering to grow the literal too. Passing the count as a template argument
// means the caller (which already has it, as controlCount) and the checker can
// never disagree, and a static_assert can still evaluate it: a function
// ARGUMENT is not a constant expression, which is why this could not simply be
// passed in as a parameter.
template <std::size_t total>
constexpr bool tabsCoverAllControls (const std::array<TabSpec, numTabPages>& tabs)
    {
        std::array<int, total> seen {};

        for (const auto& tab : tabs)
            for (std::size_t i = 0; i < tab.count; ++i)
            {
                if (tab.controls[i] >= seen.size())
                    return false;

                ++seen[tab.controls[i]];
            }

        std::size_t counted = 0;

        for (const auto times : seen)
        {
            if (times > 1)
                return false;

            counted += static_cast<std::size_t> (times);
        }

        return counted == total;
    }

    // The tabSpecs table is the single source of truth for what a tab is called and
    // where it sits, so code that needs "the SETTINGS tab" looks it up here instead of
    // carrying a number that drifts the moment a tab is added or reordered. Returns
    // numTabPages when the name is not in the table, which never compares equal to a
    // currentTab in range - the caller's switches simply stay hidden. (numTabPages
    // rather than the editor's own numTabs: this is a free function, and the
    // static_assert in setCurrentTab keeps the two counts identical.)
    int index_of_tab_named (const char* name)
    {
        // tab is std::size_t because numTabPages is, and the table is indexed by
        // it. It was an int, and the comparison against the size_t bound warned
        // on every build that has -Wsign-compare on (Clang always, GCC in the
        // debug job) - a signed int that can never reach an unsigned bound is
        // the shape of a real bug even when this particular loop cannot trip it.
        for (std::size_t tab = 0; tab < numTabPages; ++tab)
            if (std::strcmp (tabSpecs[tab].name, name) == 0)
                return static_cast<int> (tab);

        return static_cast<int> (numTabPages);
    }

    juce::String formatDb (float value)
    {
        // Round to one decimal BEFORE formatting, so a value like -0.04 (which is just
        // the meter's jitter floor) prints as "0.0 dB" instead of the crooked-looking
        // "-0.0 dB".
        const auto rounded = std::round (value * 10.0f) * 0.1f + 0.0f;
        return juce::String (rounded, 1) + " dB";
    }

    // Returns the largest font up to `maxHeight` at which `text` still fits inside
    // `availableWidth`. Captions in a switch or a fixed-width label are laid out in
    // code, not by a layout engine, so a caption that grows (or a host that swaps in
    // a wider default font) silently truncates or ellipsises - and a truncated
    // caption is what made the rocker switches look broken. Measuring the string
    // against the space it actually has keeps the text whole instead.
    juce::Font shrinkingFont (const juce::String& text, float maxHeight,
                              int fontStyle, float availableWidth)
    {
        const auto makeFont = [fontStyle] (float height)
        {
            return juce::Font (juce::FontOptions (height, fontStyle));
        };

        if (availableWidth <= 0.0f || text.isEmpty())
            return makeFont (maxHeight);

        auto font = makeFont (maxHeight);

        // A safety margin for side bearing. A flat 1 px was not enough: on the narrow
        // workflow buttons (SAVE / DEL / UNDO / REDO, 38-44 px wide) a caption measured
        // as "just fitting" and then still came out as "SA..." / "D..." / "UN...",
        // because the real rendered string is a little wider than the arrangement
        // reports. The margin is now a share of the available width with a 3 px floor.
        const auto available = juce::jmax (1.0f, availableWidth
                                             - juce::jmax (3.0f, availableWidth * 0.08f));
        const auto measured = juce::GlyphArrangement::getStringWidth (font, text);

        if (measured > available)
        {
            // Scale once by the measured ratio rather than stepping down in fixed
            // decrements: one measurement, and the result is the exact size that
            // just fits instead of the next coarser size down.
            const auto ratio = available / measured;
            const auto scaled = maxHeight * juce::jlimit (0.55f, 1.0f, ratio);
            font = makeFont (scaled);
        }

        return font;
    }

    void drawScrew (juce::Graphics& g, float x, float y, const UiPalette& palette)
    {
        g.setColour (palette.border.withAlpha (0.65f));
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        g.setColour (palette.panel.brighter (0.22f));
        g.drawLine (x - 1.5f, y + 1.5f, x + 1.5f, y - 1.5f, 0.8f);
    }

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area,
                    const UiPalette& palette, float cornerSize)
    {
        const auto rect = area.toFloat();

        // Skip a panel the clip cannot reach. Without this every repaint rebuilds
        // all five gradients and re-fills all five panels however small the dirty
        // rectangle was, because the geometry and the gradient setup are paid for
        // whether or not the rasteriser then clips the result away. That fixed cost
        // per repaint is what the panel's 30 Hz animation pays, and attaching a
        // context makes every one of those repaints a render of this component.
        if (! rect.intersects (g.getClipBounds().toFloat()))
            return;

        // ------------------------------------------------------------------
        //  Drop shadow. The light source is up-and-left, consistently with the
        //  knob highlights and the screw slots, so the shadow falls down-and-
        //  right.
        //
        //  melatonin_blur draws this as one real Gaussian-ish shadow rather than a
        //  stack of three flat rounded rectangles, which is what makes the panel
        //  read as sitting ABOVE the chassis instead of being outlined on it. The
        //  library caches the blurred mask against its path and radius, so the
        //  cost is one blur the first time and a blit on every frame after - which
        //  is why it is affordable on a panel repainted at 30 Hz. Without the
        //  module the hand-drawn approximation below is unchanged.
        // ------------------------------------------------------------------
#if J37_HAS_MELATONIN_BLUR
        {
            // Same API the knob halo uses: setters, then render against a Path. A
            // rounded rectangle is built as a path so the proven render(g, Path)
            // overload is the one called, and the shadow is cached against it.
            melatonin::DropShadow panelShadow;
            panelShadow.setColor (juce::Colours::black.withAlpha (0.30f));
            panelShadow.setRadius (cornerSize + 2.0f);

            juce::Path panelPath;
            panelPath.addRoundedRectangle (rect.getX(), rect.getY(),
                                           rect.getWidth(), rect.getHeight(), cornerSize);
            panelShadow.render (g, panelPath);
        }
#else
        for (int layer = 3; layer >= 1; --layer)
        {
            // Shadows fall DOWN only. A previous pass biased the offset upward
            // (spread * 0.6 on x with spread on y was fine, but callers placed
            // captions flush with panel bottoms), so the layer stack printed as a
            // displaced halo AROUND text sitting near the edge - the "shifted
            // shadow" the panel showed under labels. Small, strictly downward,
            // tight alpha: a suggestion of depth, never a second object.
            const auto spread = static_cast<float> (layer) * 1.2f;
            g.setColour (juce::Colours::black.withAlpha (0.035f));
            g.fillRoundedRectangle (rect.translated (0.0f, spread)
                                        .expanded (spread * 0.4f),
                                    cornerSize + spread * 0.4f);
        }
#endif

        // The face: a subtle vertical gradient, brighter at the top because that
        // is where the light lands.
        juce::ColourGradient fill (palette.panel.brighter (0.10f), rect.getX(), rect.getY(),
                                  palette.panel.darker (0.10f), rect.getX(), rect.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (rect, cornerSize);

        // The top-edge highlight: one pixel, inset past the corners so it does not
        // fight the rounded edge. This is what makes the panel read as RAISED.
        g.setColour (palette.panel.brighter (0.30f).withAlpha (0.55f));
        g.drawLine (rect.getX() + cornerSize, rect.getY() + 0.5f,
                    rect.getRight() - cornerSize, rect.getY() + 0.5f, 1.0f);

        // The outline, and an inner seam. The seam is darker than the face rather
        // than lighter - it is the shadow the panel casts on the chassis, so it
        // belongs on the inside of the border.
        g.setColour (palette.border);
        g.drawRoundedRectangle (rect.reduced (0.5f), cornerSize, 1.0f);
        g.setColour (palette.panel.darker (0.35f).withAlpha (0.7f));
        g.drawRoundedRectangle (rect.reduced (3.0f), juce::jmax (1.0f, cornerSize - 2.0f), 0.7f);
    }
}

void J37LookAndFeel::drawRotarySlider (juce::Graphics& g,
                                       int x, int y, int width, int height,
                                       float sliderPos,
                                       const float rotaryStartAngle,
                                       const float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto& palette = paletteForTheme (theme);
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                 static_cast<float> (y),
                                                 static_cast<float> (width),
                                                 static_cast<float> (height)).reduced (6.0f);
    const auto centre = bounds.getCentre();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.34f;
    const auto outerRadius = radius + 8.0f;
    const auto angle = juce::jmap (sliderPos, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);
    // JUCE's Path::addCentredArc measures clockwise from 12 o'clock and places a
    // point at (sin(angle), -cos(angle)). Convert that same angle to ordinary screen
    // coordinates for the tracer, ticks and pointer; using cos(angle), sin(angle)
    // directly rotates every indicator by 90 degrees and makes the arc look crooked.
    const auto screenAngle = angle - juce::MathConstants<float>::halfPi;

    //------------------------------------------------------------------
    //  Animation layer 1: a soft halo that breathes with the compressor
    //  activity, plus a slow pulse so the panel never looks frozen.
    //------------------------------------------------------------------
    const auto breath = 0.5f + 0.5f * std::sin (animationPhase);
    const auto haloAlpha = 0.05f + activity * 0.20f * (0.6f + 0.4f * breath);
    if (haloAlpha > 0.01f)
    {
#if J37_HAS_MELATONIN_BLUR
        // melatonin_blur draws the halo as one real blurred shadow rather than a
        // stack of stroked rings. The API renders a Path, and the shadow is cached
        // against that path, so the glow costs one blur the first time the radius
        // changes and is reused on every frame after that.
        melatonin::DropShadow glow;
        glow.setColor (palette.accent.withAlpha (haloAlpha));
        glow.setRadius (6.0 + activity * 6.0 * breath);

        juce::Path haloPath;
        haloPath.addEllipse (centre.x - radius, centre.y - radius,
                             radius * 2.0f, radius * 2.0f);
        glow.render (g, haloPath);
#else
        for (int ring = 3; ring >= 1; --ring)
        {
            const auto haloRadius = outerRadius + static_cast<float> (ring) * 5.0f
                                    + activity * 4.0f * breath;
            g.setColour (palette.accent.withAlpha (haloAlpha / static_cast<float> (ring)));
            g.drawEllipse (centre.x - haloRadius, centre.y - haloRadius,
                           haloRadius * 2.0f, haloRadius * 2.0f, 1.6f);
        }
#endif
    }

    g.setColour (palette.knobEdge.withAlpha (0.22f));
    g.fillEllipse (centre.x - radius - 3.0f, centre.y - radius + 2.0f,
                   (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f);

    juce::Path scaleTrack;
    scaleTrack.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (palette.knobEdge.withAlpha (0.70f));
    g.strokePath (scaleTrack, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    juce::Path activeTrack;
    activeTrack.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                               rotaryStartAngle, angle, true);
    g.setColour (palette.accent);
    g.strokePath (activeTrack, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

    // Bright tracer dot riding the end of the active arc - the clearest "live" cue.
    const auto tracer = centre + juce::Point<float> (std::cos (screenAngle) * outerRadius,
                                                     std::sin (screenAngle) * outerRadius);
    const auto tracerPulse = 2.6f + 1.4f * breath + activity * 2.0f;
    g.setColour (palette.accent.withAlpha (0.35f));
    g.fillEllipse (tracer.x - tracerPulse * 1.9f, tracer.y - tracerPulse * 1.9f,
                   tracerPulse * 3.8f, tracerPulse * 3.8f);
    g.setColour (palette.readout);
    g.fillEllipse (tracer.x - tracerPulse, tracer.y - tracerPulse,
                   tracerPulse * 2.0f, tracerPulse * 2.0f);

    //------------------------------------------------------------------
    //  Animation layer 2: the scale ticks tremble with the transport
    //  drift, so wow and flutter are visible as well as audible.
    //------------------------------------------------------------------
    const auto driftWobble = (drift - 0.5f) * 2.0f;
    for (int tick = 0; tick <= 12; ++tick)
    {
        const auto tickAngle = juce::jmap (static_cast<float> (tick), 0.0f, 12.0f,
                                           rotaryStartAngle, rotaryEndAngle);
        const auto tickScreenAngle = tickAngle - juce::MathConstants<float>::halfPi;
        const auto major = tick % 3 == 0;
        const auto tremble = std::sin (tickScreenAngle * 3.0f + animationPhase * 1.7f)
                             * driftWobble * 1.3f;
        const auto innerRadius = outerRadius + (major ? 3.0f : 4.0f) + tremble;
        const auto outerTickRadius = innerRadius + (major ? 5.0f : 2.5f);
        const auto inner = centre + juce::Point<float> (std::cos (tickScreenAngle) * innerRadius,
                                                        std::sin (tickScreenAngle) * innerRadius);
        const auto outer = centre + juce::Point<float> (std::cos (tickScreenAngle) * outerTickRadius,
                                                        std::sin (tickScreenAngle) * outerTickRadius);
        g.setColour (palette.knobEdge.withAlpha (major ? 0.75f : 0.42f));
        g.drawLine (inner.x, inner.y, outer.x, outer.y, major ? 1.2f : 0.8f);
    }

    juce::ColourGradient face (palette.knobHighlight, centre.x - radius, centre.y - radius,
                               palette.knobFace, centre.x + radius, centre.y + radius, false);
    g.setGradientFill (face);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (palette.knobEdge.withAlpha (0.9f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
    g.setColour (palette.knobHighlight.withAlpha (0.48f));
    g.drawEllipse (centre.x - radius + 4.0f, centre.y - radius + 4.0f,
                   radius * 2.0f - 8.0f, radius * 2.0f - 8.0f, 0.8f);

    // Moving specular sweep across the knob face.
    const auto sweepAngle = animationPhase * 0.6f;
    const auto sweepCentre = centre + juce::Point<float> (std::cos (sweepAngle) * radius * 0.42f,
                                                          std::sin (sweepAngle) * radius * 0.42f);
    g.setColour (palette.knobHighlight.withAlpha (0.10f + 0.06f * breath));
    g.fillEllipse (sweepCentre.x - radius * 0.42f, sweepCentre.y - radius * 0.42f,
                   radius * 0.84f, radius * 0.84f);

    const auto pointerLength = radius * 0.62f;
    // The pointer uses the same converted screen angle as the active arc, tracer and
    // ticks, so its tip is exactly radial rather than 90 degrees away from the arc.
    const auto pointerEnd = centre + juce::Point<float> (
        std::cos (screenAngle) * pointerLength,
        std::sin (screenAngle) * pointerLength);
    g.setColour (palette.accent.darker (0.15f));
    g.drawLine (centre.x + 1.0f, centre.y + 1.0f, pointerEnd.x + 1.0f, pointerEnd.y + 1.0f, 3.0f);
    g.setColour (palette.needle);
    g.drawLine (centre.x, centre.y, pointerEnd.x, pointerEnd.y, 2.2f);
    g.setColour (palette.accent);
    g.fillEllipse (centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f);

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.85f));
        g.drawEllipse (bounds.reduced (1.5f), 1.1f);
    }
}

//==============================================================================
//  Toggle switches are drawn as small hardware rockers instead of the default
//  text-button look: a cylindrical metal body shaded by the cosine of its own
//  cross-section angle (see fillCylinderBarrel - the same profile the 3D scene
//  uses), engraved captions, a thumb that physically slides between the two
//  positions, and a status LED that lights when the switch is engaged. This is
//  what the panel's BYPASS / POLARITY / AUTO GAIN controls were missing - they
//  used to look like plain buttons that happened to hold state.
//==============================================================================
void J37LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto& palette = paletteForTheme (theme);
    const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const auto isOn = button.getToggleState();

    // The body is a cylinder lying along the switch - fillCylinderBarrel
    // shades it by the cosine of its cross-section angle - not a flat slab.
    // Engaging the switch darkens the barrel too, so the state reads through
    // the shading itself and not only through the thumb.
    fillCylinderBarrel (g, bounds,
                        palette.readout.darker (isOn ? 0.22f : 0.02f),
                        palette.readout.darker (0.42f));
    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.0f);

    // Two bands sized from the switch itself, so nothing ever fights for width:
    //   top band    - the function name, engraved across the full switch width
    //   bottom band - a short recessed track with a sliding thumb that carries the
    //                 state text; the state is always readable and the label never
    //                 truncates. The band height is a proportion of the control
    //                 (was a hardcoded 11 px, which on a 32 px switch left a crowded
    //                 15 px track carrying THREE overlapping text pieces: OFF, ON and
    //                 the thumb caption).
    const auto labelHeight = juce::jlimit (10.0f, 14.0f, bounds.getHeight() * 0.44f);
    const auto labelBand = bounds.withHeight (labelHeight);
    // The groove is inset deeper than before because the body is now a full
    // capsule: at its rounded bottom the silhouette pulls in fast, and a
    // groove that ran to the old 4 px inset would paint past the arc.
    const auto track = bounds.withTrimmedTop (labelHeight)
                            .withTrimmedLeft (7.0f).withTrimmedRight (7.0f)
                            .withTrimmedBottom (3.0f);

    // Status lamp: one explicit circular footprint, concentric at every scale. The
    // previous square slot made the lamp's visual centre depend on the band height,
    // which read as a curved/crooked LED at the smaller toggle sizes. All three layers
    // below use the same centre and only change diameter, so there is no offset bezel,
    // crescent, or elliptical edge to line up.
    const auto ledDiameter = juce::jlimit (6.0f, 8.0f, labelBand.getHeight() * 0.55f);
    const auto ledCentre = juce::Point<float> (
        labelBand.getRight() - 4.0f - ledDiameter * 0.5f,
        labelBand.getCentreY());
    const auto ledBounds = juce::Rectangle<float> (ledCentre.x - ledDiameter * 0.5f,
                                                     ledCentre.y - ledDiameter * 0.5f,
                                                     ledDiameter, ledDiameter);
    g.setColour (palette.readout.darker (0.40f));
    g.fillEllipse (ledBounds.getX(), ledBounds.getY(),
                   ledBounds.getWidth(), ledBounds.getHeight());
    if (isOn)
    {
        g.setColour (palette.status.withAlpha (0.24f));
        const auto glowBounds = ledBounds.expanded (1.5f);
        g.fillEllipse (glowBounds.getX(), glowBounds.getY(),
                       glowBounds.getWidth(), glowBounds.getHeight());
    }
    g.setColour (isOn ? palette.status : palette.knobEdge.withAlpha (0.45f));
    const auto coreBounds = ledBounds.reduced (ledDiameter * 0.28f);
    g.fillEllipse (coreBounds.getX(), coreBounds.getY(),
                   coreBounds.getWidth(), coreBounds.getHeight());

    // The function name is centred in the WHOLE label band, not in a band with the
    // lamp's footprint sliced off one side. Trimming the right edge left the caption
    // sitting about 6 px left of the switch's true centre, which is what made the label
    // and the lamp look crookedly placed. At the three real captions (BYPASS, POLARITY,
    // AUTO GAIN) the centred text clears the lamp by at least 15 px, so centring the
    // caption cannot make them collide.
    const auto captionBand = labelBand;
    g.setColour (palette.text.withAlpha (0.92f));
    g.setFont (shrinkingFont (button.getButtonText().toUpperCase(), 8.0f,
                               juce::Font::bold, captionBand.getWidth()));
    g.drawText (button.getButtonText().toUpperCase(), captionBand,
                juce::Justification::centred, true);

    // Track bed: a recessed groove with the thumb sliding between its two ends. The
    // OFF/ON stop captions were removed - the thumb's own caption is the single
    // authoritative state readout, so there is only ever one state word on screen.
    g.setColour (palette.readout.darker (0.55f));
    g.fillRoundedRectangle (track, 3.0f);

    // Thumb sized to fully cover its half of the track (was a fixed 23 px on a track
    // whose half-width changed with the control, so it could overhang the right edge).
    const auto thumbWidth = juce::jmax (12.0f, (track.getWidth() - 2.0f) * 0.5f);
    const auto thumbHeight = juce::jmax (8.0f, track.getHeight() - 2.0f);
    const auto thumbX = isOn ? track.getRight() - thumbWidth - 1.0f : track.getX() + 1.0f;
    const auto thumbY = track.getY() + 1.0f + (shouldDrawButtonAsDown ? 1.0f : 0.0f);
    const auto thumb = juce::Rectangle<float> (thumbX, thumbY, thumbWidth, thumbHeight);
    // The thumb is a small cylinder of its own riding the groove, pressed one
    // pixel into the body while the mouse is down (thumbY above already
    // shifts it): shading, not colour, carries the mechanics.
    fillCylinderBarrel (g, thumb, palette.knobFace, palette.knobEdge);
    g.setColour (palette.knobEdge.withAlpha (0.85f));
    g.drawRoundedRectangle (thumb, thumb.getHeight() * 0.5f, 1.0f);

    // State text on the thumb, fitted to the thumb. The thumb is the authoritative
    // state readout, so its text must never be the thing that gets clipped.
    g.setColour (isOn ? palette.gaugeInk.withAlpha (0.95f) : palette.secondary);
    g.setFont (shrinkingFont (isOn ? "ON" : "OFF", 7.5f, juce::Font::bold, thumb.getWidth()));
    g.drawText (isOn ? "ON" : "OFF", thumb, juce::Justification::centred, true);

    // Hover ring for mouse/keyboard focus feedback.
    if (shouldDrawButtonAsHighlighted || button.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), 5.0f, 1.0f);
    }
}

//==============================================================================
//  The deck's combo boxes (MODEL, SPEED, the workflow lists) are painted by
//  LookAndFeel_V4::drawComboBox, which reserves a 30 px arrow zone on the right -
//  but V4's positionComboBoxText was never overridden, so the value Label sat in
//  geometry inherited from wherever the last Look and Feel left it. Any list whose
//  Label geometry drifted ended up printing its text twice, a few pixels apart -
//  text on text, unreadable. Pinning the Label here gives every deck list one
//  authoritative placement: inside the arrow zone, at the combo font V4 chooses.
//==============================================================================
void J37LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, juce::jmax (0, box.getWidth() - 30), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
//  The combo value Label: juce::ComboBox paints its text with an internal
//  juce::Label through THIS virtual, so the Label's own paint must not add
//  anything the field does not want - an opaque fill would sit on top of the
//  drawn background (a flat white card over the deck's colours, exactly the
//  band the workflow lists arrived with), and a proportional face would make
//  the same entry change width between the box and the open menu.
//==============================================================================
void J37LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());

    auto area = label.getLocalBounds().toFloat();
    if (! label.isEnabled())
        g.setOpacity (0.45f);

    g.drawFittedText (label.getText(), area.toNearestInt(),
                      label.getJustificationType(),
                      juce::jmax (1, static_cast<int> (area.getHeight() / g.getCurrentFont().getHeight())),
                      label.getMinimumHorizontalScale());
}

//==============================================================================
//  Text-button captions are measured against each button's own width rather than
//  drawn at whatever size the default Look and Feel happens to choose. The
//  preset / A/B row is width-constrained at the minimum panel size, so a caption
//  that grows ("A (LIVE) *", "A/B: B") used to be ellipsised; scaling the font to
//  the button keeps the whole caption readable at any editor size.
//==============================================================================
void J37LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto& palette = paletteForTheme (theme);

    auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                             : juce::TextButton::textColourOffId);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.45f);
    if (shouldDrawButtonAsHighlighted)
        colour = colour.brighter (0.12f);
    if (shouldDrawButtonAsDown)
        colour = colour.darker (0.3f);

    // Side bearing so the fitted caption is never clipped by the rounded corners. 3 px
    // rather than 5: the workflow buttons are only 38-44 px wide, so every pixel here is
    // the difference between a full "SAVE" and an ellipsised "SA...".
    const auto bounds = button.getLocalBounds().toFloat().reduced (3.0f, 1.0f);
    const auto text = button.getButtonText();

    g.setColour (colour);
    g.setFont (shrinkingFont (text, 12.0f, juce::Font::bold, bounds.getWidth()));
    g.drawText (text, bounds, juce::Justification::centred, true);

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.7f));
        g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.0f);
    }
}

//==============================================================================
//  The IN-TAB look and feel.
//
//  See the class comment in PluginEditor.h for WHY this exists as a second
//  design rather than a smaller copy of the deck's. This is the drawing.
//==============================================================================
void J37InlineLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                         bool isButtonDown,
                                         int, int, int, int,
                                         juce::ComboBox& box)
{
    const auto& palette = paletteForTheme (theme);
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                                 static_cast<float> (width),
                                                 static_cast<float> (height)).reduced (0.5f);

    // A flat, recessed field rather than the deck's heavy bezel. The only depth
    // is a single-pixel inset at the top edge, which is what makes it read as
    // "a field you type or choose in" rather than as "a button".
    g.setColour (isButtonDown ? palette.readout.brighter (0.06f) : palette.readout);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (palette.border.withAlpha (0.85f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    // The inner top shadow: one hairline, which is the whole of the recess.
    g.setColour (palette.panel.darker (0.35f).withAlpha (0.5f));
    g.drawLine (bounds.getX() + 3.0f, bounds.getY() + 1.0f,
                bounds.getRight() - 3.0f, bounds.getY() + 1.0f, 0.8f);

    // The value is drawn LEFT-aligned, like a settings row, rather than centred
    // like a button caption - that is the single biggest thing that makes this
    // read as a list rather than as a switch. The text itself is NOT drawn here:
    // juce::ComboBox paints its own internal Label over this call, so printing
    // the value here as well drew every list twice, a few pixels apart - text on
    // text, which is the self-overlap the lists arrived with. positionComboBoxText
    // below places that Label; this override only paints the field around it.

    // A slim caret, drawn as two short strokes rather than a filled triangle:
    // at this size a filled triangle reads as a decoration, a chevron reads as
    // "there is a list here".
    const auto caretCentre = juce::Point<float> (bounds.getRight() - 9.0f, bounds.getCentreY());
    g.setColour (palette.accent);
    g.drawLine (caretCentre.x - 3.5f, caretCentre.y - 1.8f,
                caretCentre.x, caretCentre.y + 1.8f, 1.4f);
    g.drawLine (caretCentre.x, caretCentre.y + 1.8f,
                caretCentre.x + 3.5f, caretCentre.y - 1.8f, 1.4f);

    if (box.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.75f));
        g.drawRoundedRectangle (bounds.expanded (0.5f), 3.5f, 1.0f);
    }
}

juce::Font J37InlineLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    // Sized from the box's own height so the same style works in a tab cell and
    // on a larger panel without a second constant to keep in step.
    const auto height = juce::jlimit (9.0f, 14.0f, static_cast<float> (box.getHeight()) * 0.50f);
    return juce::Font (juce::FontOptions (height));
}

void J37InlineLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // The value Label, placed for the in-tab style: left-aligned with the same
    // inset the old hand-drawn text used, and trimmed on the right so a long
    // entry ellipsises before it reaches the caret. Print-the-value lives ONLY
    // here (see drawComboBox above): the base class never runs for these lists,
    // so without this override the Label kept V4's geometry - centred, 30 px
    // reserved for an arrow zone the flat field does not draw.
    label.setBounds (6, 1, juce::jmax (0, box.getWidth() - 26), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void J37InlineLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Same argument as the deck style's drawLabel: the internal Label paints
    // itself over the field, so its own paint must be ink only - no opaque
    // background card, and the same face the field's own drawing used.
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());

    auto area = label.getLocalBounds().toFloat();
    if (! label.isEnabled())
        g.setOpacity (0.45f);

    g.drawFittedText (label.getText(), area.toNearestInt(),
                      label.getJustificationType(),
                      juce::jmax (1, static_cast<int> (area.getHeight() / g.getCurrentFont().getHeight())),
                      label.getMinimumHorizontalScale());
}

juce::Font J37InlineLookAndFeel::getPopupMenuFont()
{
    // The popup is the one place the in-tab style cannot size itself from a
    // control, so it uses a fixed compact size - the same 12 px the shrinking
    // font settles on for the tab captions, so the menu matches the panel.
    return juce::Font (juce::FontOptions (12.0f));
}

void J37InlineLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    const auto& palette = paletteForTheme (theme);
    const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const auto isOn = button.getToggleState();

    // The in-tab switch is a small PILL with a sliding dot and an ON/OFF word -
    // the shape every settings panel uses, and deliberately NOT the deck's
    // rocker. The pill is a share of the control's own height so it scales with
    // the cell, and it never grows taller than a comfortable touch target.
    const auto pillHeight = juce::jlimit (12.0f, 18.0f, bounds.getHeight() * 0.72f);
    const auto pillWidth = juce::jlimit (pillHeight * 1.7f, bounds.getWidth() * 0.46f,
                                          pillHeight * 2.4f);
    const auto pill = juce::Rectangle<float> (bounds.getRight() - pillWidth,
                                              bounds.getCentreY() - pillHeight * 0.5f,
                                              pillWidth, pillHeight);

    // The track is a cylinder too (same shading helper), with the accent as
    // its face when on: the two-colour fill keeps the state readable at a
    // glance, and the curvature is what makes it read as hardware rather
    // than as a settings-app pill.
    fillCylinderBarrel (g, pill,
                        isOn ? palette.accent.withAlpha (0.85f)
                             : palette.readout.darker (0.35f),
                        isOn ? palette.accent.darker (0.35f)
                             : palette.readout.darker (0.55f));
    g.setColour (palette.border.withAlpha (0.85f));
    g.drawRoundedRectangle (pill, pillHeight * 0.5f, 1.0f);

    // The dot. It rides the track's two ends; when on it is the readout colour
    // on the accent, and when off it is the panel colour on the recess.
    const auto dotDiameter = juce::jmax (6.0f, pillHeight - 4.0f);
    const auto dotX = isOn ? pill.getRight() - dotDiameter - 2.0f : pill.getX() + 2.0f;
    const auto dotY = pill.getCentreY() - dotDiameter * 0.5f + (shouldDrawButtonAsDown ? 0.7f : 0.0f);
    g.setColour (isOn ? palette.readout : palette.knobFace);
    g.fillEllipse (dotX, dotY, dotDiameter, dotDiameter);
    g.setColour (palette.knobEdge.withAlpha (0.7f));
    g.drawEllipse (dotX, dotY, dotDiameter, dotDiameter, 0.8f);

    // The caption sits LEFT of the pill and is fitted to the space that is
    // actually left, so a long name shrinks rather than running under the pill.
    const auto captionArea = bounds.withRight (pill.getX() - 4.0f);
    if (captionArea.getWidth() > 4.0f)
    {
        const auto caption = button.getButtonText().toUpperCase();
        g.setColour (palette.secondary);
        g.setFont (shrinkingFont (caption,
                                   juce::jlimit (8.0f, 11.0f, bounds.getHeight() * 0.42f),
                                   juce::Font::bold, captionArea.getWidth()));
        g.drawText (caption, captionArea, juce::Justification::centredLeft, true);
    }

    // The state word, on the track itself, so the control is unambiguous even
    // for a user who cannot tell the two track colours apart.
    g.setColour (isOn ? palette.readout : palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, pillHeight * 0.42f),
                                               juce::Font::bold)));
    const auto wordArea = pill.withTrimmedLeft (isOn ? 2.0f : dotDiameter + 3.0f)
                               .withTrimmedRight (isOn ? dotDiameter + 3.0f : 2.0f);
    g.drawText (isOn ? "ON" : "OFF", wordArea, juce::Justification::centred, false);

    if (shouldDrawButtonAsHighlighted || button.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.45f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), 4.0f, 1.0f);
    }
}

//==============================================================================
FirstAudioProcessorEditor::LevelMeter::LevelMeter (juce::String meterTitle, juce::String meterSubtitle)
    : title (std::move (meterTitle)), subtitle (std::move (meterSubtitle))
{
    setOpaque (false);
}

void FirstAudioProcessorEditor::LevelMeter::setLoudness (float peakDbIn, float rmsDbIn,
                                                        float lufsIn, float vuDbIn,
                                                        float combinedDbIn, bool clippingIn)
{
    constexpr float floorDb = -70.0f;
    constexpr float frameSeconds = 1.0f / 30.0f;

    peakDb = juce::jlimit (floorDb, 6.0f, peakDbIn);
    rmsDb = juce::jlimit (floorDb, 6.0f, rmsDbIn);
    lufs = juce::jlimit (floorDb, 6.0f, lufsIn);
    vuDb = juce::jlimit (floorDb, 6.0f, vuDbIn);
    combinedDb = juce::jlimit (floorDb, 6.0f, combinedDbIn);
    clipping = clippingIn;

    // Each view gets a ballistic matched to what it represents: the combined reading and
    // the RMS rise quickly and fall slowly, the VU is left slow because that is its whole
    // point, and the K-weighted LUFS is already time-averaged in the DSP.
    //
    // Re-tuned after the "meters look crooked" report: the raw DSP values arrive at
    // 30 Hz with block-to-block jitter of several dB, which made the numbers twitch in
    // a lopsided way. Each rise/fall pair is now an order of magnitude apart (a clean
    // exponential envelope shape) and the RMS/LUFS rows share the same rise so the
    // two "honest average" rows move as one steady pair.
    const auto smoothTowards = [] (float current, float target, float rise, float fall)
    {
        const auto coefficient = target > current ? rise : fall;
        return current + (target - current) * coefficient;
    };

    displayedRms = smoothTowards (displayedRms, rmsDb, 0.50f, 0.05f);
    displayedLufs = smoothTowards (displayedLufs, lufs, 0.50f, 0.04f);
    displayedVu = smoothTowards (displayedVu, vuDb, 0.15f, 0.08f);
    displayedCombined = smoothTowards (displayedCombined, combinedDb, 0.35f, 0.07f);

    // Peak hold, so a fast transient stays readable instead of flashing past. The
    // release is a fixed dB-per-second slope rather than an exponential decay, so the
    // hold marker falls in a straight, predictable line.
    if (peakDb >= peakHoldDb)
    {
        peakHoldDb = peakDb;
        peakHoldTime = 0.65f;
    }
    else if (peakHoldTime > 0.0f)
    {
        peakHoldTime = juce::jmax (0.0f, peakHoldTime - frameSeconds);
    }
    else
    {
        peakHoldDb = juce::jmax (peakDb, peakHoldDb - 12.0f * frameSeconds);
    }

    repaint();
}

void FirstAudioProcessorEditor::LevelMeter::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (palette.raised.darker (0.12f));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (palette.text);
    // A local text scale derived from the meter's own size (not from the display), so
    // the labels grow with the dial instead of staying at a fixed 8-10 px and becoming
    // unreadable in a large cell or oversized in a small one.
    const auto textScale = juce::jlimit (0.85f, 1.6f,
                                         static_cast<float> (getWidth()) / 190.0f * 1.0f);

    // The title and subtitle get two NON-OVERLAPPING bands carved out of one
    // proportional top block. The old code removed 20*scale for the title, then
    // removed 32*scale and trimmed 18*scale off that for the subtitle, so the
    // subtitle actually started at 14*scale - six pixels (scaled) BEFORE the title
    // ended and the two lines printed on top of each other.
    const auto topBlock = juce::roundToInt (32.0f * textScale);
    const auto titleHeight = juce::roundToInt (18.0f * textScale);
    auto localBounds = getLocalBounds();
    const auto titleBand = localBounds.removeFromTop (titleHeight);
    const auto subtitleBand = localBounds.removeFromTop (topBlock - titleHeight);

    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (10.0f * textScale, juce::Font::bold)));
    g.drawText (title, titleBand, juce::Justification::centred, false);

    g.setColour (palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
    g.drawText (subtitle, subtitleBand, juce::Justification::centred, false);

    // Everything below is derived from this component's own bounds rather than from
    // fixed pixel offsets, so the dial stays centred and correctly scaled whether the
    // meter is a tall single-column VU or one cell of the 2 x 2 grid, and at any
    // display scale factor. `bounds` is the same rectangle the frame above was drawn
    // from, so it is reused rather than recomputed.
    //
    // The vertical budget is now PROPORTIONAL rather than a stack of measured fixed
    // slices: the dial face gets a fixed share of what is left after the title block,
    // and the five readout rows share the remainder EVENLY. A previous scheme reserved
    // per-row heights that scaled with textScale, so on short, narrow cells the rows
    // bunched against the bottom edge while the dial sat high - the lopsided look that
    // made the input/output meters read as crooked next to the compressor bars.
    constexpr int readoutRowCount = 5;

    // The dial takes a fixed share of the body, and the readout rows then take
    // whatever is genuinely LEFT below it. The old code computed the row block from a
    // fixed share of the body and only afterwards clamped it upwards to fit, which had
    // two consequences: the clamp pushed the first row back up against the dial, and
    // the last row still ended up ONE pixel from the rounded bottom border at every
    // editor size - the loudness numbers were literally touching the panel edge.
    // Taking the row height from the space that actually remains below the dial keeps
    // the dial, the rows and the border from fighting over the same pixels.
    const auto bodyHeight = juce::jmax (72, getHeight() - topBlock);
    const auto faceHeight = juce::roundToInt (static_cast<float> (bodyHeight) * 0.48f);
    const auto face = juce::Rectangle<float> (bounds.getX(),
                                              bounds.getY() + static_cast<float> (topBlock),
                                              bounds.getWidth(), static_cast<float> (faceHeight));

    constexpr float rowGapAbove = 4.0f;
    constexpr float rowBottomMargin = 3.0f;
    const auto rowsTop = face.getBottom() + rowGapAbove;
    const auto availableForRows = juce::jmax (static_cast<float> (readoutRowCount * 10),
                                              bounds.getBottom() - rowsTop - rowBottomMargin);
    // floor, not round: rounding UP would make the block taller than the space that
    // was actually measured, and the clamp below would then drag the whole block back
    // up on top of the dial. Flooring guarantees the rows start below the dial and
    // still leave the bottom margin.
    const auto readoutRowHeight = juce::jmax (10, static_cast<int> (availableForRows
                                                                    / readoutRowCount));
    const auto usedReadout = readoutRowHeight * readoutRowCount;
    const auto readoutTop = juce::jmin (rowsTop,
                                        bounds.getBottom() - static_cast<float> (usedReadout)
                                          - rowBottomMargin);
    const auto rowHeight = readoutRowHeight;

    const auto centreX = face.getCentreX();
    // The dial is a half circle sitting on the lower edge of the face area.
    const auto radius = juce::jmin (face.getWidth() * 0.5f - 10.0f, face.getHeight() * 0.72f);
    const auto centreY = face.getBottom() - 6.0f;
    const auto startAngle = juce::MathConstants<float>::pi;
    const auto endAngle = juce::MathConstants<float>::twoPi;

    const auto dial = juce::Rectangle<float> (centreX - radius - 8.0f,
                                              centreY - radius - 10.0f,
                                              (radius + 8.0f) * 2.0f,
                                              radius + 16.0f);

    // The whole dial is clipped to the face it belongs to. This is the guarantee that
    // nothing drawn here can ever continue past the dial box and into the readout rows
    // below: whatever the gauge geometry does, the PEAK/RMS/LUFS/VU/AVG rows are simply
    // unreachable. The box itself ends exactly on the face's bottom edge, so the clip
    // never cuts the visible instrument. (JUCE 9 removed GraphicsStateSaver; saveState /
    // restoreState are the pair it wrapped, and the restore is called explicitly below.)
    g.saveState();
    // 1 px of slack so rounding the float face to an integer clip cannot shave the
    // bottom border off the dial's own rounded box. The readout rows start 4 px below
    // the face, so this still leaves 3 px of guaranteed clearance.
    g.reduceClipRegion (face.toNearestInt().reduced (1, 1));

    g.setColour (palette.knobEdge.withAlpha (0.55f));
    g.fillRoundedRectangle (dial, 4.0f);
    g.setColour (palette.gaugeFace);
    g.fillRoundedRectangle (dial.reduced (3.0f), 3.0f);
    g.setColour (palette.border);
    g.drawRoundedRectangle (dial.reduced (3.0f), 3.0f, 0.9f);

    // The tick marks and their labels only make sense once the dial is big enough to
    // separate them, so they are drawn from an adaptive count rather than always ten.
    const auto tickCount = radius > 34.0f ? 10 : 6;

    // The gauge arc, and the reason its angles are NOT the startAngle/endAngle the tick
    // loop below uses. JUCE's arc convention is 0 radians = 12 o'clock with the angle
    // increasing clockwise - Point::getPointOnCircumference is (x + r*sin a, y - r*cos a)
    // - so the top half, left over the top to the right, is 3/2*pi to 5/2*pi. Handing
    // addCentredArc the tick loop's pi..2*pi put the sweep on the WRONG half: it began
    // at the BOTTOM of the dial, bulged out to the left and ended at the top, which is
    // the black half-circle that hung below the meter and ran through the readout rows.
    // 3/2*pi -> 5/2*pi traces the same left -> top -> right path that the ticks and the
    // needle describe with (cos a, sin a) over pi..2*pi, so all three agree exactly.
    juce::Path scaleArc;
    scaleArc.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                            juce::MathConstants<float>::pi * 1.5f,
                            juce::MathConstants<float>::pi * 2.5f,
                            true);
    g.setColour (palette.gaugeInk.withAlpha (0.8f));
    g.strokePath (scaleArc, juce::PathStrokeType (1.1f));

    for (int tick = 0; tick <= tickCount; ++tick)
    {
        const auto fraction = static_cast<float> (tick) / static_cast<float> (tickCount);
        const auto angle = juce::jmap (fraction, startAngle, endAngle);
        const auto major = tick % 2 == 0;
        const auto outerRadius = radius - 1.0f;
        const auto innerRadius = radius - (major ? 8.0f : 4.0f);
        const auto outer = juce::Point<float> (centreX + std::cos (angle) * outerRadius,
                                               centreY + std::sin (angle) * outerRadius);
        const auto inner = juce::Point<float> (centreX + std::cos (angle) * innerRadius,
                                               centreY + std::sin (angle) * innerRadius);
        g.setColour (palette.gaugeInk);
        g.drawLine (inner.x, inner.y, outer.x, outer.y, major ? 1.1f : 0.7f);

        if (major)
        {
            const auto db = -20 + juce::roundToInt (fraction * 23.0f);
            const auto labelRadius = juce::jmax (4.0f, radius - 18.0f);
            const auto labelPoint = juce::Point<float> (
                centreX + std::cos (angle) * labelRadius,
                centreY + std::sin (angle) * labelRadius);
            g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
            g.drawText (juce::String (db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<int> (juce::roundToInt (labelPoint.x - 10.0f),
                                              juce::roundToInt (labelPoint.y - 5.0f), 20, 11),
                        juce::Justification::centred, false);
        }
    }

    // (The old standalone "VU" caption was removed: it sat at centreY + 13, exactly
    // where the readout rows begin, and printed over the PEAK and RMS rows. The scale
    // is already named by the meter subtitle and by the VU row itself.)
    // The needle rides the combined 25 %-each reading, so the dial shows one trustworthy
    // number while the text block below shows exactly how that number was arrived at.
    const auto needleDb = juce::jlimit (-20.0f, 3.0f, displayedCombined);
    const auto needleFraction = (needleDb + 20.0f) / 23.0f;
    const auto needleAngle = juce::jmap (needleFraction, startAngle, endAngle);
    const auto needleLength = juce::jmax (4.0f, radius - 13.0f);
    const auto needleEnd = juce::Point<float> (
        centreX + std::cos (needleAngle) * needleLength,
        centreY + std::sin (needleAngle) * needleLength);
    g.setColour (palette.needle.withAlpha (0.9f));
    g.drawLine (centreX + 0.8f, centreY + 0.8f, needleEnd.x + 0.8f, needleEnd.y + 0.8f, 2.0f);
    g.setColour (palette.needle);
    g.drawLine (centreX, centreY, needleEnd.x, needleEnd.y, 1.5f);
    g.setColour (palette.accent);
    g.fillEllipse (centreX - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
    g.restoreState();   // end of the dial clip - the readout rows below draw unclipped

    // -------------------------------------------------------------------
    //  Four-way readout.
    //
    //  Each scale gets its own row because they answer different questions and their
    //  spread is meaningful: PEAK against LUFS shows how dynamic the material is, and
    //  the gap between VU and RMS shows how much transient content is present.
    //  The combined row is the equal-weighted average of the four, which is what the
    //  needle and the dial are driven from.
    // -------------------------------------------------------------------
    // The readout rows are inset from the meter's rounded border by a share of the
    // cell's own width instead of a hardcoded 2 px. At the minimum panel size the
    // left-aligned label started and the right-aligned value ended two pixels from the
    // border, which is what made the numbers look pasted onto the panel edge and the
    // column read as crooked.
    const auto rowInset = juce::jmax (5, juce::roundToInt (static_cast<float> (getWidth()) * 0.055f));
    const auto rowWidth = juce::jmax (1, getWidth() - 2 * rowInset);

    const auto drawReadoutRow = [&] (const juce::String& label, float value,
                                     juce::Colour valueColour, int row)
    {
        const auto rowTop = juce::roundToInt (readoutTop) + row * rowHeight;
        const auto rowArea = juce::Rectangle<int> (rowInset, rowTop, rowWidth, rowHeight);
        g.setColour (palette.secondary);
        g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
        g.drawText (label, rowArea, juce::Justification::centredLeft, false);

        // The value column uses a monospaced face so digits are all the same width:
        // with a proportional font the right-aligned numbers kept shifting sideways
        // frame to frame ("1" is narrower than "8"), which read as the meter being
        // crooked. With fixed-width digits the column has a clean, stable right edge.
        g.setColour (valueColour);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  8.5f * textScale, juce::Font::bold)));
        g.drawText (formatDb (value), rowArea, juce::Justification::centredRight, false);
    };

    // The clipping lamp: only the peak view can clip, so it is flagged next to that row.
    const auto peakColour = clipping ? juce::Colour::fromRGB (208, 82, 58) : palette.text;
    drawReadoutRow ("PEAK dB", peakHoldDb, peakColour, 0);
    drawReadoutRow ("RMS", displayedRms, palette.text, 1);
    drawReadoutRow ("LUFS", displayedLufs, palette.accent, 2);
    drawReadoutRow ("VU", displayedVu, palette.text, 3);

    // Hairline on the exact boundary between the four measurement rows and the
    // summary row (neither row's centred text reaches its own edge), so the average
    // reads as a summary.
    g.setColour (palette.border.withAlpha (0.45f));
    g.drawHorizontalLine (juce::roundToInt (readoutTop) + 4 * rowHeight,
                          static_cast<float> (rowInset),
                          static_cast<float> (getWidth() - rowInset));

    // The summary row drives the needle: it is the equal-weighted average of the four
    // views above (the old "MIX 25%" caption wrongly suggested it had something to do
    // with the MIX control).
    drawReadoutRow ("AVG", displayedCombined, palette.accent, 4);
}
//==============================================================================
FirstAudioProcessorEditor::CompressorMeter::CompressorMeter (juce::String meterTitle,
                                                             juce::String stageCaption)
    : title (std::move (meterTitle)), caption (std::move (stageCaption))
{
    setInterceptsMouseClicks (false, false);
}

void FirstAudioProcessorEditor::CompressorMeter::setReduction (float reductionDb, float newActivity)
{
    // Smooth downwards instantly (so gain reduction is never under-reported) but
    // let the bar fall back gracefully, like a real VU-driven reduction needle.
    const auto clamped = juce::jlimit (-12.0f, 0.0f, reductionDb);
    const auto coefficient = clamped < displayedDb ? 0.55f : 0.10f;
    displayedDb += (clamped - displayedDb) * coefficient;

    activity += (juce::jlimit (0.0f, 1.0f, newActivity) - activity) * 0.22f;

    repaint();
}

void FirstAudioProcessorEditor::CompressorMeter::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (palette.raised.darker (0.12f));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    // Text and ladder scale with this meter's own size, so the reduction display stays
    // legible whether it is one cell of the 2 x 2 grid or a large panel, without any
    // dependence on the host's display scale.
    const auto textScale = juce::jlimit (0.85f, 1.6f, static_cast<float> (getWidth()) / 190.0f);

    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (10.0f * textScale, juce::Font::bold)));
    g.drawText (title, getLocalBounds().removeFromTop (juce::roundToInt (22.0f * textScale)),
                juce::Justification::centred, false);

    const auto trackTop = 26.0f * textScale;
    const auto trackBottom = static_cast<float> (getHeight()) - 46.0f * textScale;
    const auto trackLeft = 12.0f;
    const auto trackRight = static_cast<float> (getWidth()) - 12.0f;
    const auto trackHeight = juce::jmax (24.0f, trackBottom - trackTop);

    const auto centreX = 0.5f * (trackLeft + trackRight);
    const auto barWidth = juce::jmin (30.0f * textScale, (trackRight - trackLeft) * 0.44f);
    const auto barLeft = centreX - barWidth * 0.5f;

    // Segmented LED ladder: it reads as a classic hardware reduction display. The
    // segment count is chosen from the available height so the ladder fills the cell
    // instead of leaving gaps in a tall layout or overlapping in a short one.
    const auto segments = juce::jlimit (8, 24, juce::roundToInt (trackHeight / (7.0f * textScale)));
    const auto segmentGap = 2.0f * textScale;
    const auto segmentHeight = (trackHeight - segmentGap * static_cast<float> (segments - 1))
                               / static_cast<float> (segments);
    const auto litFraction = juce::jlimit (0.0f, 1.0f, -displayedDb / 12.0f);
    const auto litSegments = static_cast<int> (std::ceil (litFraction * static_cast<float> (segments)));

    for (int segment = 0; segment < segments; ++segment)
    {
        const auto y = trackTop + static_cast<float> (segment) * (segmentHeight + segmentGap);
        const auto lit = segments - segment <= litSegments;
        const auto position = static_cast<float> (segment) / static_cast<float> (segments - 1);

        juce::Colour segmentColour = palette.status;
        if (position > 0.45f) segmentColour = palette.accent;
        if (position > 0.78f) segmentColour = juce::Colour::fromRGB (196, 74, 52);

        if (lit)
        {
            g.setColour (segmentColour.withAlpha (0.30f));
            g.fillRoundedRectangle (barLeft - 2.0f, y - 1.0f, barWidth + 4.0f,
                                    segmentHeight + 2.0f, 2.5f);
            g.setColour (segmentColour);
        }
        else
        {
            g.setColour (palette.border.withAlpha (0.35f));
        }

        g.fillRoundedRectangle (barLeft, y, barWidth, segmentHeight, 2.0f);
    }

    // Activity glow behind the ladder so the meter still shows the compressor
    // breathing during quiet passages where no reduction is happening.
    if (activity > 0.01f)
    {
        const auto glowHeight = trackHeight * juce::jlimit (0.0f, 1.0f, activity);
        g.setColour (palette.accent.withAlpha (0.10f + activity * 0.10f));
        g.fillRoundedRectangle (barLeft - 5.0f, trackBottom - glowHeight,
                                barWidth + 10.0f, glowHeight, 3.0f);
    }

    const auto readoutY = getHeight() - juce::roundToInt (42.0f * textScale);
    const auto reducing = displayedDb < -0.05f;
    g.setColour (reducing ? palette.accent : palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              10.0f * textScale, juce::Font::bold)));
    // Same rounding-then-printing rule as the level meters' formatDb: near-zero
    // reductions must read "0.0 dB", never "-0.0 dB".
    g.drawText (formatDb (displayedDb),
                juce::Rectangle<int> (2, readoutY, getWidth() - 4, 16),
                juce::Justification::centred, false);

    // Each meter now belongs to exactly one stage, so the caption names the point in
    // the chain that stage sits at rather than splitting one readout into IN and OUT.
    g.setColour (palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
    g.drawText (caption,
                juce::Rectangle<int> (2, readoutY + 16, getWidth() - 4, 13),
                juce::Justification::centred, false);
}

//==============================================================================
// The shared label look: text, font size and weight, colour, alignment, and no
// mouse interception so a caption never steals a click from the control under it.
// This is a member function, not a constructor-local lambda, because resized()
// styles the knob-grid section captions too and a lambda declared in the
// constructor is out of scope there - which is what broke the Linux, Windows and
// macOS builds with C2065.
void FirstAudioProcessorEditor::styleLabel (juce::Label& label, const juce::String& text,
                                            float size, juce::Colour colour,
                                            bool bold, juce::Justification justification)
{
    // The small captions beside the engine switches and under the member rows
    // used to render a size (or two) larger than the control they name, which is
    // what read as off on this panel - a caption should sit UNDER its control,
    // not beside it. One size down from what the call site asks for, with a
    // slight squeeze floor so a long caption ellipsises instead of spilling into
    // the neighbour's cell.
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, size - 1.0f),
                                                  bold ? juce::Font::bold : juce::Font::plain)));
    label.setJustificationType (justification);
    label.setMinimumHorizontalScale (0.82f);
    label.setColour (juce::Label::textColourId, colour);
    label.setInterceptsMouseClicks (false, false);
}

//==============================================================================
// The GL switch is the one panel control whose LOOK is its state, so both come from
// the context. It is a plain TextButton, not a ToggleButton, which is what made it
// look wrong in both states: it was the only TextButton in the panel never given a
// themed buttonColourId, so it kept LookAndFeel_V4's light default background on a
// dark panel, and because getToggleState() is always false for it, drawButtonText
// read textColourOffId for ON as well - so GL ON and GL OFF were the same grey with
// different words in them.
void FirstAudioProcessorEditor::styleGlButton (bool isOn)
{
    const auto& palette = paletteFor (darkTheme);

    glButton.setColour (juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
    glButton.setColour (juce::TextButton::buttonOnColourId, isOn ? palette.accent : palette.raised);

    // drawButtonText picks between textColourOnId and textColourOffId with
    // getToggleState(), which is always false here, so OFF is the id that matters.
    glButton.setColour (juce::TextButton::textColourOffId, isOn ? palette.readout : palette.text);
    glButton.setColour (juce::TextButton::textColourOnId, isOn ? palette.readout : palette.text);
    glButton.repaint();
}

void FirstAudioProcessorEditor::syncGlSwitchState()
{
    // Read the context, never a cached flag: attachTo() returns void, so the only
    // honest answer to "did it work" is to ask afterwards. The 3D transport takes
    // the same answer, which is what stops the panel from showing a lit button
    // beside a blank window - or a 3D transport on a machine where the user has
    // turned the accelerator off.
    const auto isOn = openGLContext.isAttached();

    glButton.setButtonText (isOn ? "GL ON" : "GL OFF");
    styleGlButton (isOn);
    tapeScene.setSceneEnabled (isOn);

    // The texture image is palette-aware (the ink follows the theme), so a
    // theme flip rebuilds it on the spot.
    rebuildPanelTextureLayers();
}

// The arithmetic texture's doses, one call for both renderers (see paint() and
// TapeScene::panelTexture): grain = static tooth, shimmer = live tape grain,
// wear = the blotches. The shimmer term carries the drive and the gain
// reduction, so the surface is ALIVE where the machine pushes signal - the
// same weights the fragment shader reads.
void FirstAudioProcessorEditor::configureTextureWeights (float drive, float gainReduction)
{
    constexpr float grainWeight  = 0.040f;
    constexpr float shimmerBase  = 0.015f;
    constexpr float shimmerDrive = 0.030f;
    constexpr float wearWeight   = 0.030f;

    // The doses above were tuned on the dark panel, where a faint light ink
    // reads against a near-black face. On the light panel the SAME alpha of a
    // DARK ink sits on a bright face and mostly vanishes - the user's own
    // report: the texture simply was not there. So light-theme doses are
    // multiplied up: both renderers receive the boosted weights (this feeds
    // the 2D image AND the scene's uTextureWeights), so GPU and software
    // panels keep showing the same surface.
    const auto doseBoost = darkTheme ? 1.0f : 2.2f;

    panelTextureDrive = juce::jlimit (0.0f, 1.0f, drive);
    panelTextureGainReduction = juce::jlimit (0.0f, 1.0f, gainReduction);

    const auto shimmer = (shimmerBase + shimmerDrive * panelTextureDrive) * doseBoost;
    panelTextureWeights = { grainWeight * doseBoost, shimmer, wearWeight * doseBoost };

    tapeScene.setTextureWeights (grainWeight * doseBoost, shimmer, wearWeight * doseBoost);
}

// One image per resize: the static grain (hash of screen pixels) and the wear
// blotches (value noise over a coarse grid), both at the design doses. Built at
// half resolution and scaled on draw, which is what keeps the one-time cost at
// a fraction of a second and the per-frame cost at a single blit.
void FirstAudioProcessorEditor::rebuildPanelTextureLayers()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
    {
        panelTextureImage = {};
        return;
    }

    constexpr float grainWeight = 0.040f;
    constexpr float wearWeight  = 0.030f;

    // Light theme prints DARK ink on a bright face, where the same alpha
    // nearly disappears - so the static layers are boosted by the same
    // factor configureTextureWeights uses for the live ones. The two dose
    // sets have to agree or the static grain and the shimmer read as two
    // different materials.
    const auto doseBoost = darkTheme ? 1.0f : 2.2f;
    const auto grainDose = grainWeight * doseBoost;
    const auto wearDose  = wearWeight * doseBoost;

    const auto w = juce::jmax (1, bounds.getWidth() / 2);
    const auto h = juce::jmax (1, bounds.getHeight() / 2);
    auto image = juce::Image (juce::Image::ARGB, w, h, true);

    const auto hashOf = [] (float x)
    {
        return std::fmod (std::abs (std::sin (x) * 43758.5453123f), 1.0f);
    };

    // Grain: the shader's hash(face * 7.5) against half-res pixel coordinates,
    // one centered ink dot per cell, centered on the panel colour so the layer
    // only adds texture, never tint.
    const auto grainInk = paletteFor (darkTheme).text;
    const auto grainEdge = paletteFor (darkTheme).knobEdge;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto n = hashOf (static_cast<float> (x * y) * 7.5f
                                   + static_cast<float> (x) * 1.7f
                                   + static_cast<float> (y) * 2.3f);
            const auto bright = n > 0.5f;
            const auto alpha = grainDose * std::abs (n - 0.5f) * 2.0f;
            image.setPixelAt (x, y, (bright ? grainInk : grainEdge)
                                        .withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
        }

    // Wear: big soft blotches from the shader's value-noise grid, drawn as
    // translucent ellipses over the grain.
    juce::Graphics imageGraphics (image);
    for (int gy = 0; gy < 5; ++gy)
        for (int gx = 0; gx < 9; ++gx)
        {
            const auto fi = static_cast<float> (gy * 9 + gx);
            const auto n = hashOf (fi * 4.19f + 2.17f);
            if (n < 0.55f)
                continue;

            const auto cx = (static_cast<float> (gx) + hashOf (fi * 1.61f)) / 9.0f;
            const auto cy = (static_cast<float> (gy) + hashOf (fi * 2.71f)) / 5.0f;
            const auto size = 30.0f + 40.0f * hashOf (fi * 3.37f);
            const auto alpha = wearDose * (n - 0.5f) * 2.0f;

            imageGraphics.setColour (grainEdge.withAlpha (
                juce::jlimit (0.0f, 1.0f, alpha)));
            imageGraphics.fillEllipse (cx * static_cast<float> (w) - size * 0.5f,
                                       cy * static_cast<float> (h) - size * 0.5f,
                                       size, size);
        }

    panelTextureImage = std::move (image);
}

// The three transport keys plus the momentary spindown button. Same rule as the tab
// bar: a plain TextButton's getToggleState() is always false, so the lit key's
// colours are written to BOTH the ...Id and the ...OnId pair. START and SPINUP share
// the START key's lamp, because START is what the engine advances into PLAY - the
// key that is lit is the state that is running.
int FirstAudioProcessorEditor::currentTransportState() const
{
    // Read through the parameter, not a local mirror, so an automation lane moving
    // the transport repaints the panel on the next frame. convertFrom0to1() gives
    // the choice index back as a 0..2 float for a three-entry AudioParameterChoice.
    if (auto* parameter = audioProcessor.parameters.getParameter ("transport"))
        return juce::jlimit (0, 2,
                             juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())));

    return 1;
}

void FirstAudioProcessorEditor::styleTransportButtons()
{
    const auto& palette = paletteFor (darkTheme);
    const auto state = lastShownTransport;

    const auto styleKey = [&palette] (juce::TextButton& button, bool isOn)
    {
        button.setColour (juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::buttonOnColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::textColourOffId, isOn ? palette.readout : palette.text);
        button.setColour (juce::TextButton::textColourOnId, isOn ? palette.readout : palette.text);
        button.repaint();
    };

    // STOP is lit when the machine is at rest; PLAY when it is running (including
    // the internal SPINUP state, which is a running platter); START is lit while the
    // capstan is actively climbing, which is the one moment the two are different.
    styleKey (transportStopButton, state == 0);
    styleKey (transportPlayButton, state == 1);
    styleKey (transportStartButton, state == 2);
}

//==============================================================================
//  Interface sounds: attaching the four voices to the panel's controls.
//
//  Every control that has a distinct physical feel gets the voice that matches
//  it, so the panel sounds like hardware rather than like a generic UI:
//
//    switches and toggles   a CLICK, because that is what a rocker does
//    the momentary keys     PRESS and RELEASE, because they are two events
//    every knob             a DETENT, but ONLY on the value change - not on
//                           every mouse move, which would be a machine-gun
//
//  The knobs are the interesting case. A real detented pot ticks as it passes
//  each step, so the sound is driven by the VALUE crossing a boundary rather
//  than by the mouse moving. juce::Slider::onValueChange fires for exactly that
//  (a real value change), and it does not fire for a mouse move that leaves the
//  value where it was - which is precisely the distinction wanted. The rate is
//  then limited, because a fast drag would otherwise fire dozens of ticks a
//  second and sound like a buzz rather than a detent.
//==============================================================================
void FirstAudioProcessorEditor::attachInterfaceSounds()
{
    // A switch or a momentary key: on click. juce::Button::onClick covers both
    // the TextButtons and the ToggleButtons, so one lambda wires the whole panel.
    const auto attachClick = [this] (juce::Button& button, UiSoundEngine::Voice voice)
    {
        // The button's own onClick is left alone: this listener rides ALONGSIDE
        // whatever the button already does, so a control whose click changes the
        // machine still sounds right without its handler being rewritten.
        button.addMouseListener (this, false);
        juce::ignoreUnused (voice);
    };
    juce::ignoreUnused (attachClick);

    // The switches and keys, wired through onClick wrappers. Each lambda calls
    // the existing handler only if one is already set - capturing nothing but
    // `this`, so there is no ownership question and nothing to disconnect.
    const auto playClick = [this] { uiSounds.trigger (UiSoundEngine::Voice::click); };

    // juce::Button* named explicitly: the members mix ToggleButton and
    // TextButton, and an initializer_list cannot be deduced from a
    // heterogeneous braced list - the common base is what the loop wants.
    const std::array<juce::Button*, 15> stateButtons { { &bypassButton, &deltaButton, &polarityButton, &autoGainButton,
                          &modeCycleButton, &delaySyncButton,
                          &themeButton, &glButton, &savePresetButton, &deletePresetButton,
                          &copyAButton, &copyBButton, &compareButton,
                          &undoButton, &redoButton } };
    for (auto* button : stateButtons)
        button->onStateChange = [this, button, playClick, previous = button->onStateChange]
        {
            playClick();
            if (previous != nullptr)
                previous();
        };

    // The transport keys are TextButtons with an onClick rather than a toggle
    // state, so they take the press/release pair instead of the click - which is
    // exactly how a transport key on a deck behaves.
    transportStopButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };
    transportPlayButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };
    transportStartButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };

    // The tab buttons and the spindown key: the tab bar is a row of switches and
    // the spindown is a momentary hold, so each takes its own voice.
    for (auto& tab : tabButtons)
        tab.onStateChange = [this, previous = &tab] (void)
        {
            juce::ignoreUnused (previous);
            uiSounds.trigger (UiSoundEngine::Voice::click);
        };

    spindownButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };

    // Every knob: a detent per value change, rate-limited so a fast drag ticks
    // rather than buzzes. The counter is a member rather than a lambda capture
    // because a `mutable` capture inside a loop would give every knob its own
    // independent budget, and a drag that moved two knobs would tick twice as
    // often.
    for (auto& slider : controls)
    {
        slider.onValueChange = [this]
        {
            if (uiSoundTickCountdown > 0)
            {
                --uiSoundTickCountdown;
                return;
            }

            // At 30 timer frames a second, decrementing once per frame through
            // the existing timer gives roughly a 100 ms gate - about ten ticks a
            // second, which is a detent rather than a machine-gun.
            uiSoundTickCountdown = uiSoundTickGateFrames;
            uiSounds.trigger (UiSoundEngine::Voice::detent);
        };
    }

    // The combo boxes (the deck's type switches): a click when the selection
    // actually changes, which is what flipping a rotary switch feels like.
    //
    //  THE OLD CODE WAS THE PRESET BUG. `box->onChange = ...` ASSIGNS the
    //  std::function, so for presetBox and userPresetBox this line REPLACED the
    //  handler the constructor had installed two hundred lines earlier - the
    //  one that applies the preset through the processor. attachInterfaceSounds
    //  runs last, so the sound wrapper won: a click played, and the machine
    //  kept the values it had, on every preset, every time. The buttons in this
    //  file never had this bug because they were already wrapped as a CHAIN
    //  (previous + play) - the boxes now take exactly that chain too: the
    //  previous handler first, the click on top.
    for (auto* box : { &tapeTypeBox, &valveTypeBox, &ampTypeBox, &transformerTypeBox,
                       &digitalTypeBox, &vinylTypeBox, &vinylSpeedBox, &speedBox,
                       &instrumentBox, &oversamplingBox, &presetBox, &userPresetBox,
                       &delayTypeBox, &delayRateBox, &vinylGenerationBox,
                       &vinylTurntableBox, &vinylCartridgeBox })
        if (box != nullptr)
        {
            const auto previous = box->onChange;
            box->onChange = [this, playClick, previous]
            {
                if (previous != nullptr)
                    previous();
                playClick();
            };
        }
}

void FirstAudioProcessorEditor::refreshModeButtonCaption()
{
    // The caption names the live mode, read from the parameters rather than from
    // a button-local bool: a session load, an undo or a preset can all change the
    // modes behind the panel's back, and the caption must follow all of them.
    const auto modernOn = isModeOn ("modern_mode");
    const auto lofiOn   = isModeOn ("lofi_mode");
    modeCycleButton.setButtonText (modernOn ? "MODE: MODERN"
                                  : lofiOn  ? "MODE: LO-FI"
                                            : "MODE: OFF");

    // The button's background is drawn by the base Look and Feel - this class
    // overrides captions, not button backgrounds - so the state has to arrive as
    // colours: lit with the accent while a mode is engaged, the raised panel
    // colour at OFF, the same language the transport keys and the GL switch
    // speak. Without this the button kept the base Look and Feel's own grey,
    // which matched nothing else on the panel.
    const auto& palette = paletteFor (darkTheme);
    const auto modeOn = modernOn || lofiOn;
    modeCycleButton.setColour (juce::TextButton::buttonColourId,
                               modeOn ? palette.accent : palette.raised);
    modeCycleButton.setColour (juce::TextButton::buttonOnColourId,
                               modeOn ? palette.accent : palette.raised);
    modeCycleButton.setColour (juce::TextButton::textColourOffId,
                               modeOn ? palette.readout : palette.text);
    modeCycleButton.setColour (juce::TextButton::textColourOnId,
                               modeOn ? palette.readout : palette.text);
}

void FirstAudioProcessorEditor::styleSpindownButton()
{
    const auto& palette = paletteFor (darkTheme);
    const auto held = lastShownSpindown;

    // Held uses the needle colour rather than the accent: a spindown is a deliberate
    // fault (the platter is being starved of power), and the panel's one other red is
    // the meter needle, which is the same idea - something is being pushed.
    const auto heldColour = palette.needle;
    spindownButton.setColour (juce::TextButton::buttonColourId, held ? heldColour : palette.raised);
    spindownButton.setColour (juce::TextButton::buttonOnColourId, held ? heldColour : palette.raised);
    spindownButton.setColour (juce::TextButton::textColourOffId, held ? palette.readout : palette.secondary);
    spindownButton.setColour (juce::TextButton::textColourOnId, held ? palette.readout : palette.secondary);
    spindownButton.repaint();
}

void FirstAudioProcessorEditor::styleTabButtons()
{
    const auto& palette = paletteFor (darkTheme);

    for (int tab = 0; tab < numTabs; ++tab)
    {
        auto& button = tabButtons[static_cast<std::size_t> (tab)];
        const auto isOn = tab == currentTab;

        button.setColour (juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::buttonOnColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::textColourOffId, isOn ? palette.readout : palette.secondary);
        button.setColour (juce::TextButton::textColourOnId, isOn ? palette.readout : palette.secondary);
        button.repaint();
    }
}

void FirstAudioProcessorEditor::setCurrentTab (int newTab)
{
    // The tab table and the control list are two views of one set of knobs, and this is
    // the place that depends on it holding. numTabs and tabSpecs drifting apart would
    // leave a tab with no table entry; a control listed by no tab (or by two) would be
    // a knob on the panel that is unreachable or duplicated.
    static_assert (tabSpecs.size() == static_cast<std::size_t> (numTabs),
                   "numTabs and tabSpecs must describe the same number of tabs");
    static_assert (tabsCoverAllControls<controlCount> (tabSpecs),
                   "every knob must be listed by exactly one tab");

    currentTab = juce::jlimit (0, numTabs - 1, newTab);

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        const auto onActiveTab = controlIsInTab (i, currentTab);
        controls[i].setVisible (onActiveTab);
        controlLabels[i].setVisible (onActiveTab);
    }

    // The deck's non-knob switches are FULL MEMBERS of their tabs: resized() lays
    // them out on the same grid the knobs use AND re-asserts which of them are on
    // screen - the visibility note lives beside that layout. Without the resized()
    // call a tab switch changed currentTab and drew nothing, and without
    // styleTabButtons() the selected tab never took its selected colours.
    styleTabButtons();
    resized();
}

//==============================================================================
//==============================================================================
//  Translation.
//
//  juce::LocalisedStrings is a lookup table and nothing more: hand it a string,
//  it gives back the translation if there is one and the string itself if there
//  is not. That last half is the whole design here.
//
//  The obvious way to use it is with short keys - "DRIVE_TIP" in a table, and
//  TRANS("DRIVE_TIP") at the call site - and that is what the JUCE examples
//  do. It does not suit THIS panel. There are eighty-seven parameter tooltips
//  here, each two or three sentences long, and keying them would mean inventing
//  eighty-seven key names, writing all eighty-seven English strings a second
//  time in a table beside the code, and keeping the two copies in step - with
//  the failure mode being a tooltip that reads "DRIVE_TIP" to the user, or one
//  that has quietly drifted from the text it was written from.
//
//  So the English IS the key. A tooltip is written once, where it is used, in
//  English, and a translation is a line in Source/Translations/uk.txt mapping
//  that exact sentence to its Ukrainian. Nothing to rename, nothing to keep in
//  step, and a tooltip that has not been translated yet shows its English rather
//  than a key - which is the correct thing to show, and invisible as a bug.
//
//  Keyed entries still work, and are used for the strings that have no natural
//  English sentence to key on. The format is JUCE's, unchanged:
//      "KEY" = "value";
//==============================================================================
// Named xlat rather than tr: this file already has a `tr (float, int)` that
// formats a number, and it is declared after one of its own uses - so a second
// `tr` at file scope leaves that use resolving to neither. Renaming is cheaper
// than untangling which one a given call meant.
static juce::String xlat (const juce::String& text)
{
    // Not LocalisedStrings::translate - that is an instance method, and the
    // object it would need is the one this call is meant to be looking up. The
    // static entry point is translateWithCurrentMappings, which reaches the
    // table setCurrentMappings installed and returns the key unchanged when
    // there is no mapping for it.
    return juce::LocalisedStrings::translateWithCurrentMappings (text);
}

void FirstAudioProcessorEditor::setTip (juce::SettableTooltipClient& component, const juce::String& english)
{
    tooltipSources.push_back ({ &component, english });
    component.setTooltip (xlat (english));
}

void FirstAudioProcessorEditor::setTippedSentence (juce::SettableTooltipClient& component,
                                                   const juce::String& englishSentence,
                                                   const juce::String& englishHints)
{
    // The knob tooltips are sentence + interaction hints in ONE string, but the
    // lookup key is the sentence alone. Storing the concatenation was the bug:
    // "INPUT - ... 0 dB. Hold Shift for..." is not in the table, so the language
    // switch replayed the English every time no matter how complete the file was.
    // Here the sentence is registered and shown separately, the hints are a
    // constant that xlat()s on the spot, and updateTooltips() re-translates
    // each half on its own key.
    tooltipSources.push_back ({ &component, englishSentence });
    component.setTooltip (xlat (englishSentence) + xlat (englishHints));
}

void FirstAudioProcessorEditor::updateTooltips()
{
    // The English is replayed through the CURRENT table rather than the text
    // that is on the component being overwritten - translating an already
    // translated string would be a lookup that can never hit, because no key in
    // the table is a translation.
    for (const auto& source : tooltipSources)
        if (source.component != nullptr)
            source.component->setTooltip (xlat (source.english));
}

void FirstAudioProcessorEditor::applyLanguage (int languageIndex)
{
    // The tables are compiled into the binary by juce_add_binary_data (see the
    // TRANSLATION_FILES list in CMakeLists.txt), so there is nothing to install
    // beside the plugin and nothing for the user to delete. Adding a language is
    // one file plus one line in that list.
    const char* text = nullptr;
    int size = 0;

    if (languageIndex == 1)
    {
        text = BinaryData::uk_txt;
        size = BinaryData::uk_txtSize;
    }
    else
    {
        text = BinaryData::en_txt;
        size = BinaryData::en_txtSize;
    }

    // setCurrentMappings TAKES OWNERSHIP and deletes whatever it was given
    // before, so this is the whole of the memory management: no delete, no
    // member to keep, and no leak on the second switch. The second argument is
    // "this text is already in the wrong encoding", which is false - the files
    // are UTF-8, and the plugin reads them as such.
    juce::LocalisedStrings::setCurrentMappings (
        new juce::LocalisedStrings (juce::String::createStringFromData (text, size), false));

    // The language selector's own rows are keyed strings, and they are added to
    // the box BEFORE this function first runs - through a table that did not
    // exist yet, so the box showed its raw keys ("LANGUAGE_...") where the
    // language names belong. Re-apply the rows through the table just installed:
    // the items exist on every path that reaches here (the constructor adds them
    // before its first call), and a later switch re-translates them the same way.
    languageBox.changeItemText (1, xlat ("LANGUAGE_ENGLISH"));
    languageBox.changeItemText (2, xlat ("LANGUAGE_UKRAINIAN"));
    languageBox.repaint();

    updateTooltips();
}

void FirstAudioProcessorEditor::loadNeuralModelFromFile()
{
    // A model is chosen from a file, so a plain asynchronous file chooser is the
    // whole dialogue. `chooser` is captured by the lambda so the chooser stays
    // alive for the duration of the callback, which is what makes the launch
    // safe to fire and forget.
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load a neural model",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.json;*.rtn;*");

    // browserFlags, not `flags`: a local name that shadows nothing is cheaper
    // than proving no future member will ever be called flags (C4458).
    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (browserFlags, [this, chooser] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        const auto json = file.loadFileAsString();
        if (audioProcessor.loadNeuralModel (json))
        {
            loadedNeuralName = file.getFileNameWithoutExtension();
        }
        else
        {
            // A bad file leaves whatever was loaded before in place, and the
            // readout says so rather than silently showing the old name.
            loadedNeuralName = {};
            neuralStatusLabel.setText ("INVALID MODEL", juce::dontSendNotification);
            lastShownNeuralStatus = "INVALID MODEL";
            return;
        }

        lastShownNeuralStatus = {};   // force the next refresh to repaint the label
        refreshNeuralStatus();
    });
}

void FirstAudioProcessorEditor::refreshNeuralStatus()
{
    // The processor is the source of truth, not the label: a model can be
    // cleared from either button, so the readout follows whatever the stage
    // actually holds. It reports in two states only - "NO MODEL" when the stage
    // is inert, and the loaded file's name when it is not - because those are
    // the two things the NEURAL knob cannot say by itself.
    const auto status = audioProcessor.hasNeuralModel() && loadedNeuralName.isNotEmpty()
                            ? loadedNeuralName
                            : juce::String ("NO MODEL");

    if (status == lastShownNeuralStatus)
        return;

    lastShownNeuralStatus = status;
    neuralStatusLabel.setText (status, juce::dontSendNotification);
    neuralStatusLabel.setColour (juce::Label::textColourId,
                                 audioProcessor.hasNeuralModel()
                                     ? paletteFor (false).accent
                                     : paletteFor (false).secondary);
}

FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setResizable (true, true);

    // The editor is deliberately sized in LOGICAL units and the DPI scale is NOT applied
    // here. JUCE already handles display scaling for plugin editors: component coordinates
    // are logical, and the host (or JUCE on the desktop) multiplies them by the display
    // scale when it maps them to physical pixels. Multiplying setSize() by the display
    // scale as well applies it a second time, which is why the window opened far too large
    // - on a 150 % display this asked for 1860 x 1230 physical pixels for a 1240 x 820
    // panel. The layout code below is all relative to getLocalBounds(), so it needs no
    // knowledge of DPI at all.
    //
    // Sizing has TWO independent jobs, and confusing them is what left the editor unable
    // to fit on a laptop screen:
    //
    //   1. The FLOOR is the layout's own minimum, derived from the fixed furniture in
    //      getEditorLayout() and the six bands of controls in the deck. The deck needs
    //      752 px of width, which is 780 px of window once the 14 px outer padding is
    //      added; vertically it is the header, the deck and the space the knob grid
    //      needs for its three rows.
    //
    //   2. The OPENING size is the preferred size CLAMPED TO THE SCREEN IT OPENS ON.
    //      A fixed size fits a 27" monitor and nothing smaller, and the host is
    //      under no obligation to shrink the window for us. So the display the editor
    //      lands on decides: the preferred size when there is room, and the work area
    //      minus a frame allowance when there is not, centred so the whole panel is
    //      reachable instead of hanging off the bottom edge.
    //
    //   3. The FLOOR IS ALSO CLAMPED TO THE SCREEN, which is the part that was
    //      missing and the reason the window stopped fitting. Clamping the OPENING
    //      size was never enough: setResizeLimits is what the host reads when IT
    //      sizes the window, and what the user cannot drag below afterwards. So a
    //      768 px laptop - a work area of roughly 1366 x 728 - was handed a floor
    //      of 816, and a window that could not be made small enough to live on the
    //      screen no matter which of the two rules was applied. A window you cannot
    //      see is worse than a tight one, so the floor yields: below the layout's
    //      own minimum the panel gets cramped, which it is written to survive, and
    //      above it nothing changes at all.
    // The height is 664 because the layout adds up to exactly that: 28 of outer
    // padding, the 84 px header, an 8 px gap, the deck at its 236 px minimum,
    // another 8 px gap, and the control grid's own 300. It was 772, and the
    // hundred and eight came almost entirely from the deck refusing to be
    // shorter than its preferred 278 - which is what left a 1366x768 laptop,
    // with a work area of about 728 and 664 usable after the frame allowance,
    // unable to open the window at all. Every one of those terms is now a real
    // minimum something needs rather than a number that was written once.
    constexpr int minEditorWidth = 780;
    constexpr int minEditorHeight = 664;
    constexpr int preferredEditorWidth = 1060;
    constexpr int preferredEditorHeight = 916;

    // The horizontal margin keeps the frame off the edge of the display. The vertical
    // one covers a decorated window's title bar and border, which sit OUTSIDE the size
    // requested here - on Windows and GNOME that is another 30-45 px of screen the
    // work area has to have room for, and over-reserving it is how a window that
    // "fits" ends up smaller than it needs to be. It was 48 a side, which spent 96 px
    // of a 728 px work area on margins the frame does not actually use.
    constexpr int screenMarginX = 16;
    constexpr int screenMarginY = 32;

    const auto& displays = juce::Desktop::getInstance().getDisplays();
    juce::Rectangle<int> workArea;

    // The display under the mouse is the one the user is looking at, and the host will
    // almost certainly put the editor near it. The other two cases are the mouse sitting
    // outside every known display (a KVM switch, a locked session) and a headless build
    // with no displays at all - in both of them there is nothing sensible to clamp to, so
    // the preferred size is used unchanged.
    // The Point<float> overload: the Point<int> one is deprecated, and Clang says so on
    // every macOS build. Same screen, same result - only the coordinate type differs.
    if (const auto* display = displays.getDisplayForPoint (juce::Desktop::getMousePosition().toFloat()))
        workArea = display->userBounds.toNearestInt();
    else if (const auto* primary = displays.getPrimaryDisplay())
        workArea = primary->userBounds.toNearestInt();
    else
        workArea = displays.getTotalBounds (true);

    if (workArea.isEmpty())
    {
        setResizeLimits (minEditorWidth, minEditorHeight, 1500, 1180);
        setSize (preferredEditorWidth, preferredEditorHeight);
    }
    else
    {
        // toNearestInt() above already rounded, so subtracting the margin afterwards
        // cannot leave the window half a pixel - or a whole one - over the edge.
        const auto fittingWidth = workArea.getWidth() - 2 * screenMarginX;
        const auto fittingHeight = workArea.getHeight() - 2 * screenMarginY;

        // The floor this screen can actually honour, and the size it opens at. Both
        // read from the same two numbers, so the window cannot open at a size its
        // own limits would refuse - the version of this that clamped only the
        // opening size and left the floor alone was the bug, not a near miss of it.
        const auto floorWidth = juce::jmin (minEditorWidth, fittingWidth);
        const auto floorHeight = juce::jmin (minEditorHeight, fittingHeight);
        const auto openingWidth = juce::jlimit (floorWidth, fittingWidth, preferredEditorWidth);
        const auto openingHeight = juce::jlimit (floorHeight, fittingHeight, preferredEditorHeight);

        setResizeLimits (floorWidth, floorHeight, 1500, 1180);
        setSize (openingWidth, openingHeight);

        // Centred only when the screen forced a smaller window. At the preferred size the
        // host decides where the editor goes, and re-centring it here would fight the
        // DAW's own window management.
        if (openingWidth != preferredEditorWidth || openingHeight != preferredEditorHeight)
            centreWithSize (openingWidth, openingHeight);
    }

#if JUCE_DEBUG
    // The Melatonin component inspector, debug builds only. It is created after
    // the whole panel exists so it can walk the finished component tree; it
    // opens lazily and stays invisible unless asked for.
    inspector = std::make_unique<melatonin::Inspector> (*this, false);
#endif

    // ---------------------------------------------------------------------
    //  The language selector.
    //
    //  A ComboBox bound straight to the "language" parameter, so choosing a
    //  language is an ordinary parameter change: it is saved with the session,
    //  restored with it, and undone with everything else. There is no separate
    //  preferences file to keep in step with the host's idea of the project.
    //
    //  The listener is what makes the change take effect immediately. The
    //  parameter is what makes it survive a restart; the two are different jobs
    //  and either alone would be wrong - a parameter with no listener would
    //  store the choice and never act on it, and a listener with no parameter
    //  would apply it and forget it.
    // ---------------------------------------------------------------------
    languageBox.addItem (xlat ("LANGUAGE_ENGLISH"), 1);
    languageBox.addItem (xlat ("LANGUAGE_UKRAINIAN"), 2);
    languageBox.setLookAndFeel (&customLookAndFeel);

    languageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "language", languageBox);

    // The stored value arrives AFTER the constructor on a session load, so the
    // language is applied here from the parameter rather than assumed to be
    // English - otherwise a session saved in Ukrainian would open with
    // Ukrainian in the parameter and English on screen.
    if (auto* languageParameter = dynamic_cast<juce::AudioParameterChoice*> (
            audioProcessor.parameters.getParameter ("language")))
    {
        applyLanguage (languageParameter->getIndex());
    }
    else
    {
        applyLanguage (0);
    }

    languageBox.onChange = [this]
    {
        if (auto* languageParam = dynamic_cast<juce::AudioParameterChoice*> (
                audioProcessor.parameters.getParameter ("language")))
            applyLanguage (languageParam->getIndex());
    };

    styleLabel (languageLabel, xlat ("LANGUAGE_CAPTION"), 10.0f, paletteFor (false).secondary,
                true, juce::Justification::centredLeft);
    addAndMakeVisible (languageLabel);
    addAndMakeVisible (languageBox);

    // Tooltips appear after a third of a second of hover: quick enough to be
    // discoverable, slow enough not to flash while the user sweeps the panel.
    tooltipWindow->setMillisecondsBeforeTipAppears (350);
    createDecorativePhysics();

    styleLabel (brandLabel, "NONLIN ANALOG", 9.0f, paletteFor (false).accent, true, juce::Justification::left);
    titleLabel.setText ("Saturator", juce::dontSendNotification);

    // Serif display face with a cross-platform fallback chain. "Georgia" only exists on
    // Windows, so naming it alone made the title fall back to an arbitrary face (and an
    // unpredictable width) on macOS and Linux. FontOptions accepts a comma-separated
    // list and uses the first family that resolves, ending in a generic fallback.
    titleLabel.setFont (juce::Font (juce::FontOptions ("Georgia, Times New Roman, Times, serif",
                                                       30.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::left);
    titleLabel.setInterceptsMouseClicks (false, false);
    styleLabel (subtitleLabel, "NONLINEAR ANALOG MACHINE  /  SATURATION", 10.0f,
                paletteFor (false).secondary, true, juce::Justification::left);
    styleLabel (statusLabel, "STEREO / REAL TIME", 9.0f,
                paletteFor (false).status, true, juce::Justification::centred);
    styleLabel (deckHeadingLabel, "Deck", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);

    // Build identity, printed on the deck's heading strip. CMake resolves
    // J37_BUILD_COMMIT from `git rev-parse --short=8 HEAD` at configure time and marks
    // a dirty tree with a trailing "+"; when the sources are built outside a git
    // checkout the definition still exists and reads "unknown", so this line can never
    // be empty and the two cases are always distinguishable at a glance.
#ifdef J37_BUILD_COMMIT
    const juce::String buildId (J37_BUILD_COMMIT);
#else
    const juce::String buildId ("unknown");
#endif
    styleLabel (buildLabel, "BUILD " + buildId, 8.0f, paletteFor (false).secondary,
                true, juce::Justification::centredRight);
    styleLabel (tapeTypeLabel, "TAPE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (valveTypeLabel, "VALVE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (ampTypeLabel, "AMP", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (transformerTypeLabel, "XFMR", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (digitalTypeLabel, "DIGITAL", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (vinylTypeLabel, "VINYL", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    // The second selector of the six-type row is the turntable's motor speed; it
    // had a layout slot and a combo but was never given a caption, so "33 RPM"
    // sat under an empty label. RPM is what the box itself shows (33 / 45 / 78).
    styleLabel (vinylSpeedLabel, "RPM", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (speedLabel, "SPEED", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (deckHintLabel, "PLAY / AT SPEED", 9.0f,
                paletteFor (false).secondary, false, juce::Justification::centredLeft);
    styleLabel (controlsHeadingLabel, "TABS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (controlsHintLabel, "Shift = fine tune", 9.0f,
                paletteFor (false).secondary, false, juce::Justification::right);
    styleLabel (bpmLabel, "BPM", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (bpmReadout, xlat ("-"), 9.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    styleLabel (metersHeadingLabel, "LEVELS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (metersHintLabel, "dBFS / PEAK + RMS", 8.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (compressorLabel, "GLUE x2", 10.0f, paletteFor (false).accent,
                true, juce::Justification::right);
    styleLabel (compressorReadout, "one meter per stage", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::right);
    // The three readout pairs share one alignment discipline: the caption is
    // centred over its value, and every pair uses the SAME scheme. BPM read
    // left while these three read right, so a caption sat over the right half
    // of its number while BPM's sat over nothing - the four looked staggered
    // against each other even though each pair was internally consistent.
    styleLabel (harmonicsLabel, "HARMONICS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::centredLeft);
    styleLabel (harmonicsReadout, xlat ("even / odd"), 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    styleLabel (subfundLabel, "SUBFUND TRACK", 10.0f, paletteFor (false).accent,
                true, juce::Justification::centredLeft);
    styleLabel (subfundReadout, xlat ("idle"), 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    styleLabel (antiPhaseLabel, "ANTI-PHASE", 10.0f, paletteFor (false).accent,
                true, juce::Justification::centredLeft);
    styleLabel (antiPhaseReadout, xlat ("clean"), 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    addAndMakeVisible (antiPhaseLabel);
    addAndMakeVisible (antiPhaseReadout);

    addAndMakeVisible (brandLabel);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (subtitleLabel);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (deckHeadingLabel);
    addAndMakeVisible (buildLabel);
    addAndMakeVisible (tapeTypeLabel);
    addAndMakeVisible (valveTypeLabel);
    addAndMakeVisible (ampTypeLabel);
    addAndMakeVisible (transformerTypeLabel);
    addAndMakeVisible (digitalTypeLabel);
    addAndMakeVisible (vinylTypeLabel);
    addAndMakeVisible (vinylSpeedLabel);
    addAndMakeVisible (speedLabel);
    addAndMakeVisible (deckHintLabel);
    addAndMakeVisible (controlsHeadingLabel);
    addAndMakeVisible (controlsHintLabel);
    addAndMakeVisible (metersHeadingLabel);
    addAndMakeVisible (metersHintLabel);
    addAndMakeVisible (compressorLabel);
    addAndMakeVisible (compressorReadout);
    addAndMakeVisible (harmonicsLabel);
    addAndMakeVisible (harmonicsReadout);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmReadout);
    addAndMakeVisible (subfundLabel);
    addAndMakeVisible (subfundReadout);

    // These two lists are the grid's vocabulary, not its layout: their order no longer
    // decides which row anything sits on, or even whether it is on screen. tabSpecs
    // above says which tab each control belongs to and in which order it is drawn, and
    // resized() lays out the active tab alone. What the list order still has to agree
    // with is the defaultValues array below and controlCount in the header.
    //
    // BRIGHT and TONE are deliberately not swapped to line up with their ids: the
    // parameter called "tone" is registered as Brightness and the one called
    // "character" as Tone (see createParameterLayout), so each cell carries the name
    // the HOST shows for that parameter.
    // -----------------------------------------------------------------------
    //  THE CONTROL TABLE - one row per knob.
    //
    //  This used to be three parallel arrays (controlIds, controlNames,
    //  defaultValues) that had to be kept in step by hand, plus a tabSpecs table
    //  below that referred to them by INDEX. Adding a single knob meant editing
    //  four places and renumbering every index after the insertion point in
    //  tabSpecs - and because an index cannot be checked by the compiler against
    //  a name, the failure mode was a knob that silently showed another knob's
    //  value, or a range applied to the wrong control.
    //
    //  Now there is one row per control: its parameter id, its panel caption, its
    //  double-click reset value and the tab it belongs to. The arrays the rest of
    //  the constructor uses are BUILT from this table, so they cannot disagree
    //  with it, and the tab table below refers to controls by ID rather than by
    //  index. Adding a knob is one row here (plus its parameter and its tooltip);
    //  nothing needs renumbering.
    //
    //  `defaultValue` is the double-click reset, and it is the SAME number
    //  createParameterLayout() registers as the parameter's default - the two are
    //  two views of one fact and the row is where they meet.
    // -----------------------------------------------------------------------
    struct ControlSpec
    {
        const char* id;            // the parameter id
        const char* caption;       // the panel's short label
        double defaultValue;       // double-click reset (matches the parameter)
        const char* tab;           // the tab this knob belongs to, by NAME
    };

    // BRIGHT and TONE are deliberately not swapped to line up with their ids: the
    // parameter called "tone" is registered as Brightness and the one called
    // "character" as Tone (see createParameterLayout), so each row carries the
    // name the HOST shows for that parameter.
    //
    // Order here is the vocabulary, not the layout: tabSpecs decides which tab a
    // control sits on and in which order it is drawn. What this order still has to
    // agree with is the parameter defaults, which each row states directly.
    const std::array<ControlSpec, controlCount> controlSpecs { {
        { "input",           "INPUT",      0.0,   "MACHINE"  },
        { "drive",           "DRIVE",      0.30,  "DRIVE"    },
        { "bias",            "BIAS",       0.42,  "DRIVE"    },
        { "tone",            "BRIGHT",     0.50,  "MACHINE"  },
        { "character",       "TONE",       0.5,   "MACHINE"  },
        { "wow",             "WOW",        0.14,  "NOISE"    },
        { "flutter",         "FLUTTER",    0.18,  "NOISE"    },
        { "mix",             "MIX",        0.5,   "MACHINE"  },
        { "output",          "OUTPUT",     0.0,   "MACHINE"  },
        { "stereo_width",    "WIDTH",      0.5,   "MACHINE"  },
        { "blend",           "BLEND",      0.0,   "DRIVE"    },
        { "shape",           "SHAPE",      0.5,   "DRIVE"    },
        { "amp_bias",        "AMP BIAS",   0.50,  "DRIVE"    },
        { "sag",             "SAG",        0.0,   "DRIVE"    },
        { "presence",        "PRESENCE",   0.50,  "CHARACTER"},
        { "cabinet",         "CABINET",    0.0,   "CHARACTER"},
        { "delay_time",      "DELAY",      0.0,   "SPACE"    },
        { "delay_feedback",  "DLY LVL",    0.0,   "SPACE"    },
        { "st_offset",       "ST OFFSET",  0.0,   "SPACE"    },
        { "noise",           "NOISE",      0.50,  "NOISE"    },
        { "subfund",         "SUBFUND",    0.0,   "DRIVE"    },
        { "preamp",          "PREAMP",     0.0,   "DRIVE"    },
        { "distortion",      "DISTORT",    0.0,   "DRIVE"    },
        { "flux",            "FLUX",       0.50,  "CHARACTER"},
        { "wear",            "WEAR",       0.0,   "NOISE"    },
        { "mechanics",       "MECHANICS",  0.0,   "NOISE"    },
        { "reverb",          "REVERB",     0.0,   "SPACE"    },
        { "reverb_size",     "RVB SIZE",   0.40,  "SPACE"    },
        { "vinyl",           "VINYL",      0.0,   "VINYL"    },
        { "vinyl_crackle",   "CRACKLE",    0.50,  "VINYL"    },
        { "vinyl_rumble",    "RUMBLE",     0.35,  "VINYL"    },
        { "noise_lvl",       "NOISE LVL",  1.0,   "NOISE"    },
        { "st_link",         "ST LINK",    1.0,   "MACHINE"  },
        { "vinyl_dust",      "DUST",       0.0,   "NOISE"    },
        { "vinyl_scratch",   "SCRATCH",    0.0,   "NOISE"    },
        { "vinyl_warp",      "WARP",       0.0,   "NOISE"    },
        { "vinyl_electrical","ELECTRICAL", 0.0,   "NOISE"    },
        { "vinyl_clicks",    "CLICKS",     0.0,   "NOISE"    },
        { "in_low",          "IN LO",      0.0,   "IN EQ"    },
        { "in_mid",          "IN MID",     0.0,   "IN EQ"    },
        { "in_high",         "IN HI",      0.0,   "IN EQ"    },
        { "out_low",         "OUT LO",     0.0,   "OUT EQ"   },
        { "out_mid",         "OUT MID",    0.0,   "OUT EQ"   },
        { "out_high",        "OUT HI",     0.0,   "OUT EQ"   },
        { "di",              "DI",         0.0,   "DRIVE"    },
        { "di_load",         "DI LOAD",    0.0,   "DRIVE"    },
        { "di_transformer",  "DI XFMR",    0.0,   "DRIVE"    },
        { "in_hp_freq",      "IN HP",      20.0,  "IN EQ"    },
        { "in_lp_freq",      "IN LP",      20000.0, "IN EQ"  },
        { "in_eq_q",         "IN Q",       0.7,   "IN EQ"    },
        { "out_hp_freq",     "OUT HP",     20.0,  "OUT EQ"   },
        { "out_lp_freq",     "OUT LP",     20000.0, "OUT EQ"  },
        { "out_eq_q",        "OUT Q",      0.7,   "OUT EQ"   },
        { "delay_pingpong",  "PING-PONG",  0.0,   "SPACE"    },
        { "transient_attack","ATK",        0.0,   "DYN"      },
        { "transient_sustain","SUS",       0.0,   "DYN"      },
        { "transient_mix",   "TR MIX",     0.0,   "DYN"      },
        { "neural_mix",      "NEURAL",     0.0,   "DYN"      }
    } };

    // The three arrays the constructor below works from, DERIVED from the table
    // above so they cannot disagree with it. Each is sized by controlCount, so a
    // table that is too short or too long is a compile error rather than a silent
    // out-of-bounds read.
    juce::StringArray controlIds;
    juce::StringArray controlNames;
    std::array<double, controlCount> defaultValues {};

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        controlIds.add (controlSpecs[i].id);
        controlNames.add (controlSpecs[i].caption);
        defaultValues[i] = controlSpecs[i].defaultValue;
    }

    // Cross-check the table's `tab` column against the tabSpecs table below.
    //
    // The two tables describe the same partition from opposite ends - one says
    // "which tab does this knob belong to", the other "which knobs does this tab
    // hold" - and without this check they could disagree silently: a knob could
    // name DYNAMICS in this table and be listed under NOISE in tabSpecs, and the
    // panel would simply draw it on the tab tabSpecs chose while the comment here
    // said otherwise. The rebuild still works (tabSpecs is what the grid reads),
    // which is exactly why nothing else would catch it.
    //
    // This runs once, at construction, in a debug build only - the same discipline
    // the parameter/preset audit follows. It is the one place both tables are in
    // scope: controlSpecs is local to this constructor and tabSpecs is a namespace
    // constant, so a static_assert cannot span them.
    for (std::size_t i = 0; i < controlCount; ++i)
    {
        const auto* declaredTab = controlSpecs[i].tab;
        const auto listedTab = [&] () -> const char*
        {
            for (const auto& spec : tabSpecs)
                for (std::size_t c = 0; c < spec.count; ++c)
                    if (spec.controls[c] == i)
                        return spec.name;

            return nullptr;
        }();

        jassert (listedTab != nullptr);
        jassert (listedTab != nullptr && std::strcmp (declaredTab, listedTab) == 0);

        // jassert compiles away in a release build, which would leave both
        // locals unused - and the project builds with -Wall -Werror-grade warning
        // flags. Naming them as deliberately-ignored keeps the check a
        // debug-only diagnostic without turning release into a warning.
        juce::ignoreUnused (declaredTab, listedTab);
    }

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        auto& slider = controls[i];
        slider.setName (controlNames[static_cast<int> (i)]);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 2.25f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        slider.setTextBoxIsEditable (true);
        // Drag response tuned by ear: velocity-based mode throttled the first pixels
        // of every drag behind an acceleration ramp, which read as "slow knobs".
        // Direct movement plus a high sensitivity gives a full 0..1 sweep in about a
        // third of the panel width; Shift still engages JUCE's low-velocity mode for
        // fine control.
        slider.setMouseDragSensitivity (1400);
        slider.setVelocityBasedMode (true);
        slider.setVelocityModeParameters (1.0, 2, 0.06, true, juce::ModifierKeys::shiftModifier);
        slider.setPopupDisplayEnabled (true, true, this);
        slider.setScrollWheelEnabled (true);
        slider.setDoubleClickReturnValue (true, defaultValues[i]);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setColour (juce::Slider::textBoxOutlineColourId, paletteFor (false).border);

        // A tooltip for EVERY parameter - this is what shows when the user hovers.
        // The per-name text explains what the control does and its default, and the
        // generic interaction hints ride along on every knob.
        //
        // The lambda returns the ENGLISH sentence, untranslated on purpose:
        // setTip stores this exact string for the language switch and shows the
        // translation. It used to return xlat (...) + translated hints, so the
        // stored English was already Ukrainian on a Ukrainian panel - and the
        // replay's translate(already-translated) could never hit the table,
        // which is why the knob tooltips stayed English (or half-switched) after
        // a language change.
        const auto parameterTooltip = [&] (const juce::String& id) -> juce::String
        {
            if (id == "input")
                return "INPUT - output-stages the signal into the machine before the "
                       "tape. Positive pushes the tape harder for more saturation, "
                       "negative cleans up. Range -32 to +32 dB, default 0 dB.";
            if (id == "drive")
                return "DRIVE - the amount of magnetic saturation. At 0 percent the "
                       "machine is clean; higher settings bend the signal like tape "
                       "and add harmonics. Default 30 percent.";
            if (id == "bias")
                return "BIAS - the record head's ultra-sonic offset. It shapes the "
                       "even harmonics: low bias is edgy and thin, higher bias is "
                       "warmer and fuller. Default 42 percent.";
            if (id == "tone")
                return "BRIGHTNESS - a true tilt around the 1.6 kHz pivot: "
                       "low settings darken the machine (highs dip, lows lift), high "
                       "settings open it up (highs lift, lows pull back), +/-12 dB at "
                       "the extremes. 50 percent is the neutral pivot, so the spectral "
                       "balance passes through untouched. Default 50 percent.";
            if (id == "character")
                return "TONE - the machine-state macro. It crossfades the whole deck "
                       "between the classic slow machine (soft head gap, relaxed "
                       "flutter) and the fast hot machine (open top end, tight "
                       "flutter). Default 50 percent.";
            if (id == "wow")
                return "WOW - slow pitch wander of the transport, like a slightly "
                       "loose capstan. 0 percent is a perfectly steady machine. "
                       "Default 14 percent.";
            if (id == "flutter")
                return "FLUTTER - fast shimmer of the transport, like the tape "
                       "brushing the heads. 0 percent is perfectly steady. "
                       "Default 18 percent.";
            if (id == "mix")
                return "MIX - dry/wet crossfade. 0 percent is the untouched signal, "
                       "100 percent is fully through the tape. Default 50 percent.";
            if (id == "output")
                return "OUTPUT - calibrated output trim after the whole chain. "
                       "Range -32 to +32 dB, default 0 dB.";
            if (id == "stereo_width")
                return "WIDTH - stereo image after the tape. 0 percent is mono, "
                       "50 percent is the natural stereo width, 100 percent is extra "
                       "wide. Default 50 percent.";
            if (id == "subfund")
                return "SUBFUND - subharmonics. "
                       "Generates up to 8 undertones (1/2 through 1/9) "
                       "below the note when signal frequency permits, adding deep multi-layered "
                       "weight and warmth that regular saturation cannot reach. They fall away in a "
                       "staircase as they divide further, so the octave below is the strongest and "
                       "1/9 is 42 dB under it. Stages falling "
                       "below audible sub-bass (< 14-22 Hz) are smoothly attenuated to prevent DC "
                       "rumble, and the whole series stops with the note rather than continuing to "
                       "sound under it. They are added to the signal after the tape's own "
                       "saturation, so they stay clean partials instead of feeding it and coming "
                       "back out as a harmonic series. Default 0 percent - "
                       "it is a colour, not a correction.";
            if (id == "delay_time")
                return "DELAY - the spacing of a second playback head, in "
                       "milliseconds. The tape takes time to travel between the record and "
                       "playback gaps, so the signal returns as a slap rather than a dub "
                       "echo: real head spacings give tens of milliseconds, and 250 ms is "
                       "already the far end of the travel. The repeat is taken AFTER the "
                       "tape, so it inherits the machine's own bandwidth and saturation. "
                       "Default 0 ms - no second head engaged.";
            if (id == "delay_feedback")
                return "DLY LVL - how loud the second head's output is. At 0 "
                       "percent the delay is inaudible even with a time set, so TIME says "
                       "where the head is and DLY LVL says how much of it you hear. Each "
                       "pass round the tape loses top end, the way a real repeat does. "
                       "Default 0 percent.";
            if (id == "st_offset")
                return "ST OFFSET - the time offset between the two channels, "
                       "in microseconds. On a real stereo deck the two tracks are recorded "
                       "by separate head gaps a fraction of a millimetre apart and the tape "
                       "skews slightly across them, so the channels are never perfectly "
                       "aligned. Positive lags the right channel, negative the left. This "
                       "is a large part of why a tape bounce sounds wide rather than "
                       "merely equalised wide. Default 0 us.";
            if (id == "noise")
                return "NOISE MIX - how much of the noise SECTION is in the "
                       "output, exactly as MIX is how much of the tape section is. The "
                       "section it governs is every noise source at once: the tape "
                       "hiss floor, the vinyl crackle, the rumble and the groove "
                       "surface. The loaded tape formula's own character is "
                       "untouched - the formula sets the floor's shape, this sets how "
                       "much of it you hear. It is gated by the transport, so a "
                       "machine at rest is silent. Default 50 percent - the neutral "
                       "position, not a change.";
            if (id == "noise_lvl")
                return "NOISE LVL - the level of the noise SOURCES "
                       "themselves, tape and vinyl together. NOISE MIX is how much of "
                       "the noise section sits in the output; this is how loud what is "
                       "in that section actually runs, so it answers for every noise "
                       "source the machine makes, including the vinyl ones the old "
                       "NOISE control never reached. 100 percent is the calibrated "
                       "level the formulas and the record types were voiced at. "
                       "Default 100 percent.";
            if (id == "blend")
                return "BLEND - which saturation PRINCIPLE the machine bends "
                       "with. The shaper is not one curve: it is six, blended. Left is "
                       "magnetic TAPE (memory, gentle, warm), then VALVE (soft "
                       "asymmetric compression, even-harmonic warmth), then CASSETTE "
                       "(a hard early knee on narrow tape, small and loud), then AMP "
                       "(a high-gain guitar input, hard and odd-harmonic - the one "
                       "that bites), then TRANSFORMER (a passive input transformer: "
                       "pure analogue but no machine, and the only principle that "
                       "bends the low end first, because a saturating primary is a "
                       "series impedance and a series impedance bites the bottom of "
                       "the band) and right is DIGITAL (a converter's hard ceiling "
                       "with a held code: the sweep ends where the machines stop and "
                       "the conversion begins). Every curve is normalised so the "
                       "blend cannot change the level, only the character. Default 0 "
                       "percent - pure tape, exactly what earlier builds did.";
            if (id == "shape")
                return "SHAPE - how concentrated the BLEND is. Low picks "
                       "one principle at a time, so the sweep snaps from tape to valve "
                       "to cassette to amp to transformer to digital and each is "
                       "obvious. High spreads the weighting so all six contribute at "
                       "every position and the result reads as one compound machine "
                       "rather than six. Default 50 percent.";
            if (id == "amp_bias")
                return "AMP BIAS - the input valve's DC operating point, "
                       "which is the single most effective control on a real amp's "
                       "character. Cold (low) is tight and slightly crossover-distorted; "
                       "hot (high) is fat, compressed and soft. 50 percent is the "
                       "neutral centre, so it is a character sweep rather than a "
                       "one-way effect. Only audible in proportion to how much AMP is "
                       "in the blend.";
            if (id == "sag")
                return "SAG - how much the amplifier's power supply droops "
                       "under sustained demand. This is why a real amp 'gives' under a "
                       "held chord and why the attack feels spongy: the supply sags, "
                       "the gain falls a little, and then it recovers. 0 percent is a "
                       "stiff, regulated supply with no give at all; high settings are "
                       "a small amp being leaned on hard. Short transients never move "
                       "it - only sustained programme does. Default 0 percent.";
            if (id == "presence")
                return "PRESENCE - the negative-feedback network's top-end "
                       "lift, the upper-mid bite that makes an amp cut through. It sits "
                       "AFTER the clipping, so it sharpens harmonics already present "
                       "rather than generating new ones. 50 percent is the flat, "
                       "neutral position; above it sharpens, below it darkens the way "
                       "a low presence setting does. Default 50 percent.";
            if (id == "cabinet")
                return "CABINET - the speaker and its box. A resonant "
                       "low-pass, not a plain one: a peak around 110 Hz from the "
                       "cabinet's tuning and a roll-off from the cone's mass. Without "
                       "it a clipped signal is fizzy; with it, it reads as a speaker "
                       "rather than a circuit. The voicing fades in with however much "
                       "AMP is in the blend, so a pure tape setting is untouched. "
                       "Default 0 percent - a DI, the raw amp output.";
            if (id == "preamp")
                return "PREAMP - a valve input stage in FRONT of the "
                       "machine, the way a real chain has a microphone preamp before "
                       "the recorder. It is a STAGE, not a gain: driving it adds a "
                       "gentle soft clip, a transformer's low-cut and a slight top-end "
                       "lift, so it changes the colour as much as the level. It is "
                       "level-matched internally, so INPUT remains the control that "
                       "sets the operating level. Default 0 percent - no preamp.";
            if (id == "distortion")
                return "DISTORT - a diode clipper in front of the tape. "
                       "Where the saturation core BENDS, this BREAKS: a hard knee with "
                       "a pre-gain, deliberately abrupt. It sits ahead of the machine "
                       "on purpose - a distorted signal recorded to tape sounds like a "
                       "record rather than a pedal precisely because the tape smooths "
                       "what the pedal produced. Default 0 percent - off.";
            if (id == "flux")
                return "FLUX - how deep into the oxide the record head "
                       "magnetises. More flux is more low end, a stronger hysteresis "
                       "memory and a quieter floor; less is thin and bright. It is a "
                       "different axis from DRIVE: DRIVE is how hard the signal is "
                       "pushed into the curve, FLUX is how much of the medium's depth "
                       "is used. 50 percent is the calibrated, neutral flux.";
            if (id == "wear")
                return "WEAR - the state of the heads and the tape. A "
                       "used machine is not a broken one: the head gap has rounded "
                       "slightly and the oxide has lost some of its edge, so the top "
                       "end softens and the contact adds a little noise. It should "
                       "read as an old machine, not as a fault. Default 0 percent - "
                       "a fresh head and new tape.";
            if (id == "mechanics")
                return "MECHANICS - the state of the transport's moving "
                       "parts. WOW and FLUTTER set how much pitch modulation there is; "
                       "this sets how WELL the mechanism holds it. At 0 the capstan, "
                       "pinch roller and reel motors are in good order, so the "
                       "modulation is smooth and periodic. Higher settings are dry "
                       "bearings and a slack belt: irregular drift and the occasional "
                       "slip. Default 0 percent.";
            if (id == "reverb")
                return "REVERB - the room the machine is in. Tape "
                       "machines lived in rooms, and the room is part of the sound of "
                       "a recording made on one. This is a plate/room hybrid placed "
                       "AFTER the machine, so the reverb is of the processed signal "
                       "rather than feeding back into the saturation - which keeps it "
                       "clean and predictable. Default 0 percent - dry.";
            if (id == "reverb_size")
                return "RVB SIZE - how long the reverb's tail runs, and "
                       "how dark it is: a bigger room absorbs more top end per pass, "
                       "so a long tail is a darker one. That is what stops a large "
                       "setting from sounding like a metal tank. Decay and level are "
                       "separate decisions on a real reverb, which is why this is not "
                       "folded into REVERB. Default 40 percent.";
            if (id == "vinyl")
                return "VINYL - the record-playing end of the chain. A "
                       "turntable adds three things nothing else here does: surface "
                       "crackle, bearing rumble and the RIAA playback curve's low-end "
                       "lift and top-end softness. This is the overall amount; at 0 "
                       "the whole stage is bypassed. Default 0 percent.";
            if (id == "vinyl_crackle")
                return "CRACKLE - surface noise, and specifically "
                       "IMPULSES rather than hiss. A record surface is ticks, caused "
                       "by dust and by the stylus crossing the groove's "
                       "imperfections, so the generator produces sparse impulses with "
                       "a fast decay instead of continuous noise. That is the "
                       "difference between a record and a noisy tape. Default 50 "
                       "percent.";
            if (id == "vinyl_rumble")
                return "RUMBLE - the turntable's low-frequency thump, "
                       "from the bearing and the motor. It is why vinyl has a "
                       "bottom-end floor that a CD does not. It is a different noise "
                       "from the crackle and is scaled separately, because a worn "
                       "bearing and a dusty record are independent faults. Default "
                       "35 percent.";
            if (id == "vinyl_speed")
                return "VINYL SPEED - the speed of the turntable's motor, "
                       "not of the record: the VINYL TYPE describes the disc, this "
                       "describes what drives it. Each speed carries its own wow rate "
                       "and depth, so the same record wanders differently at 33 and "
                       "45, and a 78's wind-up motor wobbles hardest. 33 RPM is the "
                       "default.";
            if (id == "vinyl_dust")
                return "DUST - fine particulate in the groove. Unlike "
                       "CRACKLE's random ticks, dust is a CONTINUOUS granular "
                       "texture, band-limited high so it sits on top of the "
                       "music as grit. It follows the programme: a loud passage "
                       "sounds dirtier than a quiet one, because dust only makes "
                       "a sound when there is modulation in the groove to "
                       "disturb it. A little dust is a worn record; a lot is a "
                       "record that has been left out of its sleeve. "
                       "Default 0 percent.";
            if (id == "vinyl_scratch")
                return "SCRATCH - a deep groove wound, not dust. Where "
                       "a dust tick is random, a scratch is PERIODIC: the stylus "
                       "crosses the same damage every turn, so it arrives at the "
                       "platter rate and is heard as a repeating thud rather "
                       "than as a hiss. The rate follows VINYL SPEED, so the same "
                       "scratch repeats faster on a 45 than on a 33. "
                       "Default 0 percent.";
            if (id == "vinyl_warp")
                return "WARP - the record is not flat. A warped disc "
                       "makes the stylus ride up and down once per revolution, "
                       "so the tracking force - and therefore the output level - "
                       "breathes at the platter rate. It is a slow, cyclic "
                       "throb, which is what makes a warped record sound like it "
                       "is struggling rather than merely noisy. It modulates "
                       "what the stage passes rather than what it adds, so at 0 "
                       "percent it is exactly unity. Default 0 percent.";
            if (id == "vinyl_electrical")
                return "ELECTRICAL - the cartridge, the cable and the "
                       "earth loop. Two faults at once: MAINS HUM at the supply "
                       "frequency plus its second harmonic, which is what an "
                       "unearthed cartridge picks up from the motor and the "
                       "transformer; and EARTH STATIC, the broadband crackle of "
                       "a bad ground. The hum's two sides are deliberately not "
                       "identical - the second harmonic is in anti-phase across "
                       "the pair - which is exactly how a real earth loop "
                       "behaves. Default 0 percent.";
            if (id == "in_low")
                return "IN LO - the input equaliser's low shelf, a "
                       "low-pass split at 200 Hz with the bottom band gained. "
                       "Because it sits BEFORE the tape, lifting it drives the "
                       "saturation curve and the glue compressors harder, so it "
                       "changes what the machine DOES rather than merely the "
                       "balance. Range -12 to +12 dB, flat at 0 dB.";
            if (id == "in_mid")
                return "IN MID - the input equaliser's bell, centred at "
                       "1 kHz inside the 200 Hz - 4 kHz band. A bell rather than a "
                       "shelf, so it acts on its own pass band and leaves the two "
                       "ends where they were. Range -12 to +12 dB, flat at 0 dB.";
            if (id == "in_high")
                return "IN HI - the input equaliser's high shelf, "
                       "everything above 4 kHz. Feed the machine the top end you "
                       "want it to saturate on, rather than fixing it afterwards. "
                       "Range -12 to +12 dB, flat at 0 dB.";
            if (id == "out_low")
                return "OUT LO - the output equaliser's low shelf at "
                       "200 Hz. It sits AFTER everything the machine does and "
                       "before the output trim, so nothing downstream responds to "
                       "it: this is the neutral EQ you use to place the finished "
                       "sound. Range -12 to +12 dB, flat at 0 dB.";
            if (id == "out_mid")
                return "OUT MID - the output equaliser's bell at "
                       "1 kHz. Presence or hollow, depending which way you go, "
                       "with both ends left alone. Range -12 to +12 dB, flat at "
                       "0 dB.";
            if (id == "out_high")
                return "OUT HI - the output equaliser's high shelf "
                       "above 4 kHz. Air, or the lack of it, applied to the "
                       "finished machine. Range -12 to +12 dB, flat at 0 dB.";
            if (id == "vinyl_clicks")
                return "CLICKS - the sharp, discrete groove faults. "
                       "CRACKLE is fine surface texture and DUST is grit in the "
                       "groove; a CLICK is an actual ridge or pit that the stylus "
                       "hits as a single hard transient - a fast bipolar impact "
                       "with almost no ring, which is why it reads as a click "
                       "rather than as more crackle. Above half the travel a "
                       "fraction of the clicks becomes PERIODIC, locked to the "
                       "platter, so a badly pressed record ticks in time rather "
                       "than at random. Default 0 percent.";
            if (id == "transient_attack")
                return "ATK - the leading edge of each event. The transient "
                       "shaper looks at the signal's ENVELOPE rather than its "
                       "waveform, so it can make a hit sharper without adding a "
                       "harmonic: positive sharpens the attack (punch, snap, "
                       "click), negative softens it (rounder, less percussive). "
                       "Range -100 to +100 percent, neutral at 0.";
            if (id == "transient_sustain")
                return "SUS - what follows the attack: the body, the ring, "
                       "the room. Positive lengthens it (fuller, more sustain), "
                       "negative shortens it (tighter, more staccato). Like ATK "
                       "it moves the envelope's level rather than the waveform, "
                       "so the harmonics the machine produced are untouched. "
                       "Range -100 to +100 percent, neutral at 0.";
            if (id == "transient_mix")
                return "TR MIX - how much of the transient-shaped signal "
                       "reaches the output. At 0 the stage is absent and the "
                       "signal passes untouched; at 100 it is the fully shaped "
                       "one. In between the two are crossfaded, so the amount of "
                       "shaping can be dialled in rather than switched. "
                       "Default 0 percent.";
            if (id == "neural_mix")
                return "NEURAL - the wet/dry position of the optional "
                       "learned model. The model is not a knob: it is a file "
                       "loaded with LOAD MODEL on the DYN tab, and the network "
                       "itself (a Dense net, an LSTM, a GRU) is whatever the file "
                       "describes. This control blends the model's output with "
                       "the untouched signal, so it is always a crossfade. At 0, "
                       "or with no model loaded, the stage is transparent. "
                       "Default 0 percent.";
            return {};
        };
        setTippedSentence (slider, parameterTooltip (controlIds[static_cast<int> (i)]),
                           " Hold Shift for fine control, mouse wheel for small "
                           "steps, double-click to reset.");

        // Index 8 is OUTPUT, not 7: the control order is INPUT, DRIVE, BIAS, BRIGHT,
        // TONE, WOW, FLUTTER, MIX, OUTPUT, WIDTH, SUBFUND, DELAY, DLY LVL, ST OFFSET,
        // NOISE. The old check (i == 7) handed MIX the -32..+32 dB range and suffix -
        // which is why the Mix knob displayed dB - and left OUTPUT stuck in the
        // 0..1 percentage branch, where its slider range clipped the real +/-32 dB
        // parameter down to 0..1.
        //
        // DELAY (11) and ST OFFSET (13) are the other two exceptions: they are real
        // units (milliseconds and microseconds), not percentages, so a percentage
        // range would make the knob unable to reach either end of its own parameter.
        // The range is chosen by the control's ID, not by its position in the array.
        // It used to be `i == 11` and `i == 13`, which was correct only while the list
        // happened to be ordered that way - and when the list grew, index 11 became
        // SHAPE and index 13 became SAG, so the DELAY knob silently acquired a
        // percentage range and could no longer reach 250 ms, and ST OFFSET lost its
        // bipolar range entirely. An id cannot be renumbered by an unrelated edit.
        const auto& id = controlIds[static_cast<int> (i)];

        if (id == "input" || id == "output")
        {
            // Input and Output are both calibrated decibel trims over the same range.
            slider.setRange (minStageDb, maxStageDb, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" dB");
        }
        else if (id == "delay_time")
        {
            // DELAY: the head spacing, in milliseconds.
            slider.setRange (0.0, 250.0, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" ms");
        }
        else if (id == "st_offset")
        {
            // ST OFFSET: the inter-channel time offset, in microseconds.
            slider.setRange (-500.0, 500.0, 1.0);
            slider.setNumDecimalPlacesToDisplay (0);
            slider.setTextValueSuffix (" us");
        }
        else if (id == "in_low" || id == "in_mid" || id == "in_high"
                 || id == "out_low" || id == "out_mid" || id == "out_high")
        {
            // The two equalisers' six bands: real dB, centred on 0, and with a
            // NEUTRAL default rather than a percentage one. They are deliberately
            // NOT given the percentage text functions below - an EQ reads in dB,
            // and a band showing "50 %" for its flat position would be a control
            // whose readout does not say what it is.
            slider.setRange (-12.0, 12.0, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" dB");

            // A bipolar EQ wants its NEUTRAL position in the middle of the
            // travel, so a double-click returns to 0 rather than to one end.
            // The defaultValues table already holds 0.0 for these six; this is
            // the readout to match.
            slider.textFromValueFunction = [] (double value)
            {
                return juce::String (value, 1) + " dB";
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return juce::jlimit (-12.0, 12.0, text.getDoubleValue());
            };
        }
        else if (id == "transient_attack" || id == "transient_sustain")
        {
            // The transient shaper's two amounts are BIPOLAR and centred on 0:
            // negative softens/shortens, positive sharpens/lengthens, and 0 is
            // the neutral that leaves the stage transparent. The range is the
            // parameter's own (-1..1), shown as a signed percentage so the two
            // halves of the control read the same way they behave.
            slider.setRange (-1.0, 1.0, 0.001);
            slider.setNumDecimalPlacesToDisplay (0);
            slider.textFromValueFunction = [] (double value)
            {
                return juce::String (juce::roundToInt (value * 100.0)) + " %";
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return juce::jlimit (-1.0, 1.0, text.getDoubleValue() / 100.0);
            };
        }
        else if (id == "in_hp_freq" || id == "in_lp_freq"
                 || id == "out_hp_freq" || id == "out_lp_freq")
        {
            // The EQ filters' corners, in Hz. The two ends are the two REAL
            // bypasses: 20 Hz is below anything on a record, and 20 kHz is at or
            // above the top of the band - so the engine treats each end as "no
            // filter" rather than as a very gentle one, which is what makes a
            // neutral EQ bit-for-bit transparent.
            //
            // The high-pass is skewed toward its low end because that is where
            // the useful range is (a rumble filter lives between 20 and 120 Hz),
            // and the low-pass toward its middle for the same reason.
            if (id == "in_hp_freq" || id == "out_hp_freq")
            {
                slider.setSkewFactorFromMidPoint (100.0);
                slider.setRange (20.0, 500.0, 1.0);
            }
            else
            {
                slider.setSkewFactorFromMidPoint (8000.0);
                slider.setRange (2000.0, 20000.0, 10.0);
            }

            slider.setNumDecimalPlacesToDisplay (0);
            slider.setTextValueSuffix (" Hz");
        }
        else if (id == "in_eq_q" || id == "out_eq_q")
        {
            // The filters' corner resonance. Capped at 1.5: a resonant filter
            // ringing on a tape emulation is a fault rather than a feature, so
            // the control exists to let a steep filter be usable rather than to
            // make it sing.
            slider.setRange (0.5, 1.5, 0.01);
            slider.setNumDecimalPlacesToDisplay (2);
            slider.setTextValueSuffix (" Q");
        }
        else
        {
            slider.setRange (0.0, 1.0, 0.001);
            slider.textFromValueFunction = [] (double value)
            {
                return juce::String (juce::roundToInt (value * 100.0)) + " %";
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return juce::jlimit (0.0, 1.0, text.getDoubleValue() / 100.0);
            };
        }

        controlLabels[i].setText (controlNames[static_cast<int> (i)], juce::dontSendNotification);
        controlLabels[i].setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        controlLabels[i].setJustificationType (juce::Justification::centred);
        controlLabels[i].setInterceptsMouseClicks (false, false);

        addAndMakeVisible (controlLabels[i]);
        addAndMakeVisible (slider);
        controlAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (audioProcessor.parameters, controlIds[static_cast<int> (i)], slider);
    }

    // The three tabs the knob grid is split across. Only the active tab's knobs are on
    // screen: the other thirteen used to be laid out underneath it, invisible but still
    // sitting in the panel, and crowding the grid is what the captions were there to
    // hide. setCurrentTab() also does the first layout, now with the tab bar and a
    // single tab's grid. (resized() has already run once by now - setSize() above
    // triggers it - so this is an ordinary relayout, not a layout over unset members.)
    for (int tab = 0; tab < numTabs; ++tab)
    {
        const auto& spec = tabSpecs[static_cast<std::size_t> (tab)];
        auto& button = tabButtons[static_cast<std::size_t> (tab)];

        button.setButtonText (spec.name);
        setTip (button, spec.hint);
        button.setLookAndFeel (&customLookAndFeel);
        button.onClick = [this, tab] { setCurrentTab (tab); };
        addAndMakeVisible (button);
    }

    setCurrentTab (currentTab);

    // The same tapeStockNameList() the choice parameter is built from, so the panel
    // cannot offer a stock the parameter would refuse to select. It did once: the
    // panel drew twelve and the parameter accepted eight, so the last four were
    // drawn, implemented and unreachable. The default row is overwritten by the
    // attachment below, which syncs the box to the parameter.
    tapeTypeBox.addItemList (tapeStockNameList(), 1);
    valveTypeBox.addItemList (valveTypeNameList(), 1);
    ampTypeBox.addItemList (ampTypeNameList(), 1);
    transformerTypeBox.addItemList (transformerTypeNameList(), 1);
    digitalTypeBox.addItemList (digitalTypeNameList(), 1);
    vinylTypeBox.addItemList (vinylTypeNameList(), 1);
    vinylSpeedBox.addItemList (juce::StringArray { "33 RPM", "45 RPM", "78 RPM" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);
    tapeTypeBox.setTextWhenNothingSelected ("Select tape");
    speedBox.setTextWhenNothingSelected ("Select speed");
    setTip (tapeTypeBox, "Tape formula. Each stock bends the sound differently: "
                            "J37 is soft and classic, Ampex and Studer are hotter, "
                            "Chrome and Type 111 are cleaner, GP9 and 499 are dense "
                            "modern formulas, SM911 is the broadcast reference, "
                            "SM 468 is high-output low-noise, 888 is the hot thick "
                            "vintage stock, 815 is dark and dense, 811 is the clean "
                            "open mastering stock. The formula also shapes the glue "
                            "compressors' timing.");
    setTip (speedBox, "Transport speed. 7.5 ips is dark and loose, 15 ips is the "
                         "classic studio speed, 30 ips keeps the most top end and "
                         "the tightest glue. Speed also shapes the glue timing.");
    setTip (valveTypeBox, "VALVE TYPE - the voice of the VALVE principle. Only "
                             "audible in proportion to how much VALVE is in the "
                             "BLEND: a 12AX7 runs cold and tight, an EL34 or 6L6 "
                             "is fatter and more compressed, a 300B is the softest "
                             "and most even, a KT88 has the widest drift.");
    setTip (ampTypeBox, "AMP TYPE - the voice of the AMP principle. Only audible "
                           "in proportion to how much AMP is in the BLEND: the "
                           "Blackface is clean and firm, the Plexi bites, the AC30 "
                           "chimes, the Recto slams hardest in the second stage.");
    setTip (transformerTypeBox, "TRANSFORMER TYPE - the voice of the TRANSFORMER "
                                   "principle. Only audible in proportion to how much "
                                   "TRANSFORMER is in the BLEND: the types set how "
                                   "fast the core tracks and how early it starts to "
                                   "saturate - a nickel core bends earlier, steel "
                                   "later and harder.");
    setTip (digitalTypeBox, "DIGITAL TYPE - the voice of the DIGITAL principle. "
                               "Only audible in proportion to how much DIGITAL is in "
                               "the BLEND: the types shorten the word length and "
                               "deepen the sample-and-hold, from a 16-bit ceiling to "
                               "full bit-crush.");
    setTip (vinylTypeBox, "VINYL TYPE - the record itself, not the machine: how it "
                             "was pressed and how worn it is. Audible whenever VINYL "
                             "is up, and independent of the BLEND. Shellac 78 plays "
                             "with loud surface between every note, a dubplate is a "
                             "fresh loud lacquer, a half-speed master is nearly "
                             "silent between the grooves. The speed of the motor is "
                             "VINYL SPEED.");
    setTip (vinylSpeedBox, "VINYL SPEED - the speed of the turntable's motor, not "
                              "of the record: the VINYL TYPE describes the disc, this "
                              "describes what drives it. Each speed carries its own "
                              "wow rate and depth, so the same record wanders "
                              "differently at 33 and 45, and a 78's wind-up motor "
                              "wobbles hardest. 33 RPM is the default.");

    // The deck's readout pairs: the numbers change at 30 Hz, so a tooltip on
    // the VALUE would sit under a moving mouse on every frame. The caption
    // carries the explanation instead - it is static, and it is what a user
    // actually points at when reading a column.
    setTip (harmonicsLabel, "HARMONICS - the ratio the harmonic engine settled on: "
                               "E is the even-order share (tape and valve warmth), "
                               "O the odd-order share (transistor edge). The mix "
                               "follows the BLEND knob; this readout shows what the "
                               "blend actually produced.");
    setTip (subfundLabel, "SUBFUND TRACK - the pitch the subharmonic generator is "
                             "currently locked to. It only tracks while the machine "
                             "is fed a clean enough fundamental; 'idle - no note "
                             "tracked' means the input did not give it one.");
    setTip (antiPhaseLabel, "ANTI-PHASE - how much of the stereo pair cancels when "
                               "the channels are summed to mono. 'clean' is safe; a "
                               "percentage warns that a mono fold-down will lose "
                               "bass or body. It watches the OUTPUT side, so output "
                               "stage switches move it too.");
    setTip (bpmLabel, "BPM - the host tempo the delay and modulation sections sync "
                         "to. It follows your DAW's transport; it is a readout here, "
                         "not a setting.");
    setTip (tapeTypeLabel, "TAPE - the formula the machine's saturation is built "
                              "from. Each stock bends harmonics, compression and "
                              "noise differently. The selector below is where it is "
                              "chosen.");
    tapeTypeBox.setLookAndFeel (&customLookAndFeel);
    valveTypeBox.setLookAndFeel (&customLookAndFeel);
    ampTypeBox.setLookAndFeel (&customLookAndFeel);
    transformerTypeBox.setLookAndFeel (&customLookAndFeel);
    digitalTypeBox.setLookAndFeel (&customLookAndFeel);
    vinylTypeBox.setLookAndFeel (&customLookAndFeel);
    vinylSpeedBox.setLookAndFeel (&customLookAndFeel);
    speedBox.setLookAndFeel (&customLookAndFeel);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setButtonText ("BYPASS");
    bypassButton.setLookAndFeel (&customLookAndFeel);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "bypass", bypassButton);

    deltaButton.setClickingTogglesState (true);
    deltaButton.setLookAndFeel (&customLookAndFeel);
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "delta", deltaButton);
    addAndMakeVisible (deltaButton);

    themeButton.setButtonText ("DARK THEME");
    setTip (themeButton, "Cycle the front panel: IVORY is the paper-and-brass "
                            "studio panel, CHARCOAL the old console, and METAL a "
                            "rack unit - a cold neutral grey with a cool cyan "
                            "indicator. The button names the panel you are on; "
                            "pressing it moves to the next one.");
    themeButton.setLookAndFeel (&customLookAndFeel);
    themeButton.onClick = [this]
    {
        // The theme button now CYCLES through all three panels rather than
        // toggling two, because there are three. The order is the order they
        // read in: the paper panel, the old console, the rack unit - and it
        // wraps, so there is no "stuck" state and no second button to find.
        switch (themeChoice)
        {
            case J37LookAndFeel::ThemeChoice::ivory:
                themeChoice = J37LookAndFeel::ThemeChoice::charcoal;
                break;
            case J37LookAndFeel::ThemeChoice::charcoal:
                themeChoice = J37LookAndFeel::ThemeChoice::metal;
                break;
            case J37LookAndFeel::ThemeChoice::metal:
            default:
                themeChoice = J37LookAndFeel::ThemeChoice::ivory;
                break;
        }
        applyTheme();
    };

    // ---------------------------------------------------------------
    //  Output-stage switches: polarity invert and auto gain.
    // ---------------------------------------------------------------
    setTip (bypassButton, "Hard bypass: the tape engine and both glue compressors are "
                             "switched out. The switch is ramped, so toggling it never clicks.");
    setTip (deltaButton, "DELTA listen: the output becomes the finished signal minus the "
                            "machine's own dry signal, so you hear ONLY what the machine "
                            "adds - harmonics, glue, transport wander. MIX keeps its meaning; "
                            "digital silence means the machine is being transparent. The "
                            "switch is ramped, and BYPASS fades the difference out rather "
                            "than snapping back to the dry signal. Turn it off before you "
                            "print.");
    polarityButton.setClickingTogglesState (true);
    setTip (polarityButton, "Inverts the output polarity (180-degree phase flip). "
                               "Use it to correct an inverted source or to align two "
                               "machines feeding the same bus.");
    polarityButton.setLookAndFeel (&customLookAndFeel);
    polarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "polarity", polarityButton);
    addAndMakeVisible (polarityButton);

    autoGainButton.setClickingTogglesState (true);
    setTip (autoGainButton, "Auto gain lets the slow programme compensator restore "
                               "the level the INPUT trim dialled in. Switch it off to "
                               "keep the output exactly at the level the chain produced.");
    autoGainButton.setLookAndFeel (&customLookAndFeel);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "auto_gain", autoGainButton);
    addAndMakeVisible (autoGainButton);

    // ---------------------------------------------------------------
    //  Oversampling switch on the panel. The parameter has always been
    //  host-visible; without a control it could only be reached from the
    //  DAW's own parameter list, which is not how a premium plugin works.
    // ---------------------------------------------------------------
    styleLabel (oversamplingLabel, "OVER", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (oversamplingLabel);
    oversamplingBox.addItemList (juce::StringArray { "Off", "2x", "4x", "8x" }, 1);
    setTip (oversamplingBox, "Internal rate of the tape engine. 2x and 4x reduce the "
                                "aliasing of the magnetic shaper; the added filter delay "
                                "is reported to the host, so DAW PDC compensates.");
    oversamplingBox.setLookAndFeel (&customLookAndFeel);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "oversampling", oversamplingBox);
    addAndMakeVisible (oversamplingBox);

    // ---------------------------------------------------------------
    //  Instrument voicing selector, on the deck's second row.
    // ---------------------------------------------------------------
    styleLabel (instrumentLabel, "INSTRUMENT", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (instrumentLabel);
    instrumentBox.addItemList (juce::StringArray { "Master Bus", "Vocal", "Bass",
                                                   "Guitar", "Piano", "Drums" }, 1);
    setTip (instrumentBox, "Re-voices the machine for what is being recorded: how hard "
                              "the tape bends, how much top end survives, how loud the "
                              "floor sits and how steady the transport runs. Master Bus "
                              "is the neutral calibration. Drums is the slam "
                              "calibration - harder bend, open head, tight memory.");
    instrumentBox.setLookAndFeel (&customLookAndFeel);
    instrumentAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "instrument", instrumentBox);
    addAndMakeVisible (instrumentBox);

    // ---------------------------------------------------------------
    //  The three vinyl selectors: what was cut, what plays it, what reads it.
    //
    //  They are the same idea as TAPE TYPE and VINYL TYPE, one level up: the
    //  record TYPE says what was pressed, these say how it was cut, which deck
    //  it is on and which cartridge is in the arm. All three use the IN-TAB look
    //  and feel rather than the deck's, because they live inside a tab - see
    //  J37InlineLookAndFeel for why that is a different drawing and not a smaller
    //  copy of the same one.
    // ---------------------------------------------------------------
    const auto setUpVinylSelector = [this] (juce::Label& label, juce::ComboBox& box,
                                            const juce::String& caption,
                                            const juce::StringArray& items,
                                            const juce::String& tip,
                                            const juce::String& parameterID)
    {
        styleLabel (label, caption, 9.0f, paletteFor (false).secondary,
                    true, juce::Justification::left);
        addAndMakeVisible (label);
        box.addItemList (items, 1);
        setTip (box, tip);
        // The flat field is painted by the inline Look and Feel, but the VALUE
        // is painted by juce::ComboBox's internal Label reading
        // ComboBox::textColourId - unstyled, that stayed the base LookAndFeel's
        // black on the dark panels, which is what made these lists unreadable.
        // applyTheme() re-inks it on every theme switch; this call is what makes
        // the very first paint legible.
        box.setColour (juce::ComboBox::textColourId, paletteFor (false).text);
        box.setLookAndFeel (&inlineLookAndFeel);
        addAndMakeVisible (box);
        juce::ignoreUnused (parameterID);
    };

    setUpVinylSelector (vinylGenerationLabel, vinylGenerationBox, "GENERATION",
                        juce::StringArray { "Laquer", "Direct", "Printed" },
                        "How the record was cut and pressed. LAQUER is the reference "
                        "cut - almost none of the cutter head's own colour, loud and "
                        "clean. DIRECT is a direct-metal master, cleaner still, with "
                        "the most top end and the quietest surface of the three. "
                        "PRINTED is the pressing you actually buy: a copy of a copy, "
                        "so the top end is duller, the surface noisier and the bass a "
                        "little fuller. It also scales this stage's own noise, so a "
                        "printed record is not merely darker - it is dirtier.",
                        "vinyl_generation");

    setUpVinylSelector (vinylTurntableLabel, vinylTurntableBox, "TURNTABLE",
                        juce::StringArray { "Belt", "Direct", "Idler" },
                        "What drives the platter. BELT is an audiophile belt-drive: "
                        "the elastic belt isolates the motor, so the drive is smooth "
                        "and quiet but marginally less steady. DIRECT is a high-torque "
                        "direct-drive DJ deck: the speed is locked solid. IDLER is a "
                        "vintage idler-wheel deck, where the motor bears on the inside "
                        "of the platter through a rubber wheel and couples its own "
                        "rumble straight into the groove. Each scales the stage's "
                        "wander and its stability, so the same record wanders "
                        "differently on each deck.",
                        "vinyl_turntable");

    setUpVinylSelector (vinylCartridgeLabel, vinylCartridgeBox, "CARTRIDGE",
                        juce::StringArray { "MM", "MC", "DJ" },
                        "What reads the groove, and the largest single difference of "
                        "the three selectors. MM is a moving magnet: warm, soft on "
                        "top, broad. MC is a moving coil: more detail, a brighter and "
                        "tighter top, and a lower output - so it carries more hiss "
                        "with it. DJ is a Concorde-style DJ cart: heavier, hotter, "
                        "tracks harder, with more surface noise and a firm bottom. "
                        "Each is a gain pair plus its own noise multipliers, which is "
                        "why it sounds like a different cartridge rather than like a "
                        "tone knob.",
                        "vinyl_cartridge");

    vinylGenerationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_generation", vinylGenerationBox);
    vinylTurntableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_turntable", vinylTurntableBox);
    vinylCartridgeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_cartridge", vinylCartridgeBox);

    // ---------------------------------------------------------------
    //  Transport: STOP / PLAY / START, plus the momentary SPINDOWN hold.
    //
    //  Three buttons, not a combo. A tape machine's transport is a row of keys
    //  and the gesture matters: START and STOP are momentary, PLAY is where the
    //  machine rests. The three drive the `transport` choice through
    //  setTransportState(), and the engine writes that same parameter back when
    //  START settles into PLAY, so the lit key always matches the engine.
    //
    //  STOP-to-PLAY is handled as a TOGGLE on the same key: pressing STOP while
    //  running stops, pressing STOP again starts, which is what the request asked
    //  for and what a single transport key does on a deck. START is the explicit
    //  "spin up from rest" key and is what settles into PLAY.
    // ---------------------------------------------------------------
    styleLabel (transportLabel, "TRANSPORT", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (transportLabel);

    setTip (transportStopButton, "STOP - the capstan comes to rest. True silence: no "
                                    "hiss, no wow, no delay tail. Pressing it again while "
                                    "stopped spins the machine back up to PLAY.");
    setTip (transportPlayButton, "PLAY - normal running. This is where the machine rests "
                                     "after START has spun it up.");
    setTip (transportStartButton, "START - spins the capstan up from rest over about a "
                                      "second, the way a deck sounds when you hit play on a "
                                      "take. The pitch climbs into tune and the state settles "
                                      "into PLAY by itself when the machine reaches speed.");

    for (auto* button : { &transportStopButton, &transportPlayButton, &transportStartButton })
    {
        button->setLookAndFeel (&customLookAndFeel);
        addAndMakeVisible (*button);
    }

    transportStopButton.onClick = [this]
    {
        // Toggle on the same key: STOP stops, and a second press starts. That is
        // the "Start is Stop and vice versa" behaviour, and it keeps the key
        // useful whichever state the machine is in.
        const auto state = currentTransportState();
        if (state == 0)
            audioProcessor.setTransportState (2);   // stopped -> spin up (START)
        else
            audioProcessor.setTransportState (0);   // running -> stop
    };
    transportPlayButton.onClick = [this] { audioProcessor.setTransportState (1); };
    transportStartButton.onClick = [this] { audioProcessor.setTransportState (2); };

    // SPINDOWN: a momentary hold, so its hold edges come from the button's own
    // mouseDown/mouseUp rather than onClick. setSpindownHeld() writes both the
    // engine atomic and the host-visible parameter, so an automation lane and a
    // finger on this button are the same event.
    styleLabel (spindownLabel, "SPINDOWN", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (spindownLabel);
    spindownButton.setLookAndFeel (&customLookAndFeel);
    setTip (spindownButton, "Hold to cut the platter's power: the record runs down and "
                               "the pitch falls away, the way a turntable coasting to a stop "
                               "sounds. Release and it spins back up into PLAY. It also "
                               "brings the platter up if the machine was stopped, so the "
                               "gesture always has something to act on.");
    spindownButton.onHoldChanged = [this] (bool held)
    {
        audioProcessor.setSpindownHeld (held);
        lastShownSpindown = held;
        styleSpindownButton();
    };
    addAndMakeVisible (spindownButton);

    // ---------------------------------------------------------------
    //  DELAY TYPE - which machine the second head behaves as.
    //
    //  Three discrete machines rather than a scale: TAPE loses top end on every
    //  pass because the repeat is re-recorded, BBD is darker still with clock
    //  noise and a bandwidth that narrows as the delay lengthens, and MODERN is
    //  clean and full-bandwidth. A combo, because there is no meaningful point
    //  between them.
    // ---------------------------------------------------------------
    styleLabel (delayTypeLabel, "DLY TYPE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (delayTypeLabel);
    addAndMakeVisible (delaySyncLabel);
    delayTypeBox.addItemList (juce::StringArray { "Tape", "BBD", "Modern" }, 1);
    setTip (delayTypeBox, "What the second head's repeats sound like. TAPE loses "
                             "top end on every pass, because the repeat is recorded "
                             "onto the tape and played back through the same losses "
                             "the main path has. BBD is a bucket-brigade chip: darker "
                             "still, with clock noise on the repeats and a bandwidth "
                             "that narrows as the delay lengthens - which is what the "
                             "technology physically does. MODERN is a clean digital "
                             "delay with full bandwidth.");
    delayTypeBox.setLookAndFeel (&customLookAndFeel);
    delayTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "delay_type", delayTypeBox);
    addAndMakeVisible (delayTypeBox);

    // ---------------------------------------------------------------
    //  DELAY SYNC + RATE - the second head locked to the host's tempo.
    //
    //  SYNC switches between a free time in milliseconds (what a real head
    //  spacing gives) and a note value derived from the host's tempo. With SYNC
    //  on, the repeat lands on the beat whatever the session is at, and follows a
    //  tempo change without the user touching anything - the plugin reads the
    //  tempo from the playhead every block.
    //
    //  RATE is the note value. The list is the one a delay is actually used with:
    //  the straight divisions, the two common triplets, and the dotted quarter,
    //  which is the one that gives the classic off-beat repeat.
    // ---------------------------------------------------------------
    delaySyncButton.setClickingTogglesState (true);
    setTip (delaySyncButton, "SYNC - lock the delay to the host's tempo instead of "
                                "a time in milliseconds. With SYNC on, RATE picks a "
                                "note value and the repeat lands on the beat whatever "
                                "the session is at, following a tempo change without "
                                "the user touching anything. The tempo is read from "
                                "the host's playhead every block. With SYNC off, "
                                "DELAY is a free time in milliseconds, which is what a "
                                "real head spacing gives.");
    delaySyncButton.setLookAndFeel (&customLookAndFeel);
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "delay_sync", delaySyncButton);
    addAndMakeVisible (delaySyncButton);

    styleLabel (delayRateLabel, "RATE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (delaySyncLabel, "SYNC DELAY", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    // The GL pill carries its own caption ("GL ON" / "GL OFF"), so its LABEL is
    // never drawn - the caption lives in placeDeckSwitch's SETTINGS branch, which
    // setText()s it when that tab is laid out. The constructor keeps the text off:
    // an empty label paints nothing, so the first frame (and every frame until the
    // SETTINGS tab is opened) shows no stray "GL" floating in the deck block. The
    // label still exists as the caption slot for placeDeckSwitch to fill.
    styleLabel (glLabel, "", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (delayRateLabel);
    delayRateBox.addItemList (juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16",
                                                  "1/4 T", "1/8 T", "1/4 D" }, 1);
    setTip (delayRateBox, "RATE - the note value the delay is locked to when SYNC "
                             "is on. 1/4 is a quarter note, 1/4 T is a quarter-note "
                             "triplet (a third of a beat faster) and 1/4 D is dotted "
                             "(half again as long) - the one that gives the classic "
                             "off-beat repeat. At 120 BPM a 1/4 is 500 ms and a 1/4 D "
                             "is 750 ms.");
    delayRateBox.setLookAndFeel (&customLookAndFeel);
    delayRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "delay_rate", delayRateBox);
    addAndMakeVisible (delayRateBox);

    // ---------------------------------------------------------------
    //  The two whole-machine modes.
    //
    //  MODERN re-voices the deck for a well-maintained 1990s machine: the head
    //  losses move out of the audio band, the floor drops, the magnetic memory
    //  thins. LO-FI is the deliberate degradation - a hard 3.2 kHz limit and a
    //  sample-and-hold quantisation, applied after the protection chain so the
    //  fault survives to the output.
    //
    //  They are mutually exclusive by design: a machine cannot be both, and the
    //  engine resolves MODERN first when both are set. These handlers make the
    //  exclusivity visible on the panel rather than leaving the user to discover
    //  it by ear - turning one on releases the other, so the two lamps can never
    //  both be lit.
    // ---------------------------------------------------------------
    // One button cycles OFF -> LO-FI -> MODERN -> OFF. The two modes are
    // mutually exclusive by design, so a single state holder is honest in a way
    // two toggles plus exclusivity code never quite was: the caption always
    // names the mode that is live, and no sequence of clicks can leave both
    // engaged. The two parameters are written directly (a ButtonAttachment per
    // side would fight the cycle); the engine reads them as it always did.
    setTip (modeCycleButton, "The whole-machine mode, cycling OFF - LO-FI - "
                                "MODERN. LO-FI is the deliberate degradation: a hard "
                                "3.2 kHz bandwidth limit and sample-and-hold "
                                "quantisation on the finished sample. MODERN "
                                "re-voices the machine for a well-maintained 1990s "
                                "deck: head losses out of the audio band, a lower "
                                "noise floor, thinner magnetic memory. They are "
                                "mutually exclusive by design, so one button holds "
                                "the state.");
    modeCycleButton.setLookAndFeel (&customLookAndFeel);
    modeCycleButton.onClick = [this]
    {
        const auto modernOn = isModeOn ("modern_mode");
        const auto lofiOn   = isModeOn ("lofi_mode");

        auto nextModern = false;
        auto nextLofi = false;
        if (! modernOn && ! lofiOn)      { nextLofi = true; }        // OFF -> LO-FI
        else if (lofiOn)                 { nextModern = true; }      // LO-FI -> MODERN
        /* MODERN -> OFF */                                          // stays both false

        audioProcessor.parameters.getParameterAsValue ("modern_mode").setValue (nextModern);
        audioProcessor.parameters.getParameterAsValue ("lofi_mode").setValue (nextLofi);
        refreshModeButtonCaption();
    };
    addAndMakeVisible (modeCycleButton);
    refreshModeButtonCaption();

    // GL switch: the context is a best-effort accelerator (see the attach note
    // above), and this button hands the choice to the user - drivers, remote
    // sessions and VMs differ, and the software path draws the same panel.
    setTip (glButton, "OpenGL - GPU-accelerated rendering of the panel. Turn off if "
                         "your driver or remote session misbehaves; the software path "
                         "draws the identical panel.");
    glButton.setLookAndFeel (&customLookAndFeel);
    addAndMakeVisible (glButton);
    addAndMakeVisible (glLabel);

    // ---------------------------------------------------------------
    //  UI SOUNDS - the panel's own interface clicks.
    //
    //  A host-visible parameter, so the choice survives in the session and can
    //  be automated - and so the engine, the preset system and this switch all
    //  agree by construction rather than by being kept in step by hand. The
    //  editor reads it every frame and enables its sound engine from it.
    //
    //  It is OFF by default and deliberately quiet about being on: the engine
    //  is its own audio device, so a click never reaches the plugin's output,
    //  the DAW's meters or a render. See UiSoundEngine for why that is a hard
    //  requirement rather than a nicety.
    // ---------------------------------------------------------------
    uiSoundsButton.setClickingTogglesState (true);
    setTip (uiSoundsButton, "UI SOUNDS - the panel's own interface clicks: switches "
                               "click, keys thump, knobs tick as they cross a step. They "
                               "are synthesised and play on a SEPARATE audio device, so "
                               "they never reach the plugin's output, the DAW's meters or "
                               "a render. Off by default; a studio at 3 a.m. does not "
                               "want its interface ticking.");
    uiSoundsButton.setLookAndFeel (&inlineLookAndFeel);
    uiSoundsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "ui_sounds", uiSoundsButton);
    addAndMakeVisible (uiSoundsButton);

    // ---------------------------------------------------------------
    //  The neural model picker, on the DYNAMICS tab beside the NEURAL knob.
    //  It has no parameter and no attachment: a model is data read from a file,
    //  not a value a session or a preset carries, so the two buttons drive the
    //  processor directly and the status label reports what the stage holds.
    // ---------------------------------------------------------------
    loadNeuralButton.setLookAndFeel (&inlineLookAndFeel);
    clearNeuralButton.setLookAndFeel (&inlineLookAndFeel);
    setTip (loadNeuralButton, "LOAD MODEL - reads an RTNeural model file (its JSON "
                                 "description) and installs it as the plugin's optional "
                                 "learned nonlinearity. The network is whatever the file "
                                 "describes - a Dense net, an LSTM, a GRU - and it runs "
                                 "per channel, after the tape stage. NEURAL sets how much "
                                 "of its output reaches the signal; at 0 it is silent. "
                                 "Loading a model does not change any parameter and is "
                                 "not saved with the session.");
    setTip (clearNeuralButton, "CLEAR - releases the loaded model, returning the NEURAL "
                                  "stage to its transparent pass-through.");
    loadNeuralButton.onClick = [this] { loadNeuralModelFromFile(); };
    clearNeuralButton.onClick = [this]
    {
        audioProcessor.clearNeuralModel();
        loadedNeuralName = {};
        refreshNeuralStatus();
    };
    addAndMakeVisible (loadNeuralButton);
    addAndMakeVisible (clearNeuralButton);

    styleLabel (neuralStatusLabel, "NO MODEL", 8.5f, paletteFor (false).secondary,
                false, juce::Justification::centred);
    addAndMakeVisible (neuralStatusLabel);

    // ---------------------------------------------------------------
    //  The remaining choice lists: the DI pad, the track layout and the two
    //  EQ orders. All four are lists rather than knobs because their values are
    //  discrete settings, not points on a scale - and all four use the IN-TAB
    //  look and feel, because that is what a list inside a tab is drawn with.
    // ---------------------------------------------------------------
    const auto setUpInlineList = [this] (juce::Label& label, juce::ComboBox& box,
                                         const juce::String& caption,
                                         const juce::StringArray& items,
                                         const juce::String& tip)
    {
        styleLabel (label, caption, 9.0f, paletteFor (false).secondary,
                    true, juce::Justification::left);
        addAndMakeVisible (label);
        box.addItemList (items, 1);
        setTip (box, tip);
        // The flat field is painted by the inline Look and Feel, but the VALUE
        // is painted by juce::ComboBox's internal Label reading
        // ComboBox::textColourId - unstyled, that stayed the base LookAndFeel's
        // black on the dark panels, which is what made these lists unreadable.
        // applyTheme() re-inks it on every theme switch; this call is what makes
        // the very first paint legible.
        box.setColour (juce::ComboBox::textColourId, paletteFor (false).text);
        box.setLookAndFeel (&inlineLookAndFeel);
        addAndMakeVisible (box);
    };

    setUpInlineList (diPadLabel, diPadBox, "DI PAD",
                     juce::StringArray { "0 dB", "-10 dB", "-20 dB", "-30 dB" },
                     "The DI box's input pad, applied BEFORE its transformer. A pad "
                     "is a switch rather than a level trim because that is the "
                     "decision it makes: a hot source (an active synth, a boosted "
                     "pedal) can be plugged in without driving the box's own core "
                     "into saturation. 0 dB is no pad.");

    setUpInlineList (tracksLabel, tracksBox, "TRACKS",
                     juce::StringArray { "2", "2+3", "3" },
                     "The machine's track layout, which is geometry rather than "
                     "taste. 2 is a stereo deck: two tracks each with half the "
                     "tape, the most low end per channel and no adjacent track to "
                     "leak from. 2+3 is a four-track deck used as two on tracks 2 "
                     "and 3: a whole track's worth of tape between the channels, "
                     "so the widest spacing and the least crosstalk. 3 is a "
                     "three-track deck: narrower tracks, so less low end and a "
                     "higher noise floor for the same tape, with the most bleed "
                     "between the heads.");

    const juce::StringArray eqOrderItems { "6 dB/oct", "12 dB/oct", "18 dB/oct",
                                           "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    setUpInlineList (inputEqOrderLabel, inputEqOrderBox, "ORDER", eqOrderItems,
                     "The slope of the input EQ's high-pass and low-pass, in dB "
                     "per octave. 6 is one pole, 12 is two, up to 48 for a filter "
                     "that gets out of the way completely. The number on the panel "
                     "is the slope you get: the engine stacks one-pole sections, "
                     "one per 6 dB.");

    setUpInlineList (outputEqOrderLabel, outputEqOrderBox, "ORDER", eqOrderItems,
                     "The slope of the output EQ's high-pass and low-pass, in dB "
                     "per octave. Separate from the input EQ's because the two "
                     "equalisers make different decisions: the input one shapes "
                     "what the machine hears, the output one corrects what it "
                     "produced.");

    diPadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "di_pad", diPadBox);
    tracksAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tracks", tracksBox);
    inputEqOrderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "in_eq_order", inputEqOrderBox);
    outputEqOrderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "out_eq_order", outputEqOrderBox);

    //  The four corner switches. The corner knobs already bypass themselves at
    //  the ends of their travel, but that is a fallback rather than a control:
    //  a user who wants the equaliser's high-pass gone has to find the knob's
    //  end stop, and then cannot tell the filter from one sitting at its
    //  bypass. These are the explicit answer, and they are pill switches
    //  because they are state, like GL and the mode button.
    const auto setUpEqCornerSwitch = [this] (juce::ToggleButton& button,
                                             const juce::String& which,
                                             const juce::String& corner)
    {
        button.setClickingTogglesState (true);
        setTip (button, which.toUpperCase() + " " + corner.toUpperCase() + " ON/OFF. "
                           "The corner frequency beside it sets where the filter "
                           "turns; this says whether the filter is in the signal at "
                           "all. With the switch off the corner keeps its setting, "
                           "so turning the filter back on does not make the user "
                           "find the frequency again - which is the difference "
                           "between a switch and parking the knob at its end stop.");
        button.setLookAndFeel (&inlineLookAndFeel);
        addAndMakeVisible (button);
    };

    setUpEqCornerSwitch (inputEqHpButton, "Input EQ", "HP");
    setUpEqCornerSwitch (inputEqLpButton, "Input EQ", "LP");
    setUpEqCornerSwitch (outputEqHpButton, "Output EQ", "HP");
    setUpEqCornerSwitch (outputEqLpButton, "Output EQ", "LP");

    inputEqHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "in_hp_on", inputEqHpButton);
    inputEqLpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "in_lp_on", inputEqLpButton);
    outputEqHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "out_hp_on", outputEqHpButton);
    outputEqLpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "out_lp_on", outputEqLpButton);

    // ---------------------------------------------------------------
    //  Premium workflow bar.
    // ---------------------------------------------------------------
    styleLabel (presetHeadingLabel, "PRESET", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::centredLeft);
    addAndMakeVisible (presetHeadingLabel);

    refreshPresetList();
    // The box now starts with nothing selected on a fresh instance (honest display:
    // no factory preset has been applied yet), so it needs a placeholder for the
    // empty row rather than the base Look and Feel's blank field.
    presetBox.setTextWhenNothingSelected ("FACTORY...");
    setTip (presetBox, "Factory presets. Loading one replaces the whole machine state "
                          "in a single undoable step.");
    presetBox.setLookAndFeel (&customLookAndFeel);
    presetBox.onChange = [this]
    {
        const auto selectedId = presetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto presetIndex = selectedId - 1;

        // Re-selecting the displayed entry IS a state change whenever the machine
        // has drifted from that preset since it was loaded (the EDITED badge is
        // exactly that fact, on screen). The old guard returned whenever the
        // index matched, which made the one gesture a user reaches for after
        // tweaking - "put this preset back" - do nothing at all: the box already
        // showed the preset, the click re-picked it, and the panel kept the
        // edited state. Reload whenever the machine is dirty; skip only when
        // that preset is both loaded AND clean.
        if (presetIndex == audioProcessor.getLastPresetIndex()
            && ! audioProcessor.isPresetDirty())
            return;

        audioProcessor.applyFactoryPreset (presetIndex);
        lastShownPreset = presetIndex;
    };
    addAndMakeVisible (presetBox);

    const auto workflowButtonSetup = [&] (juce::TextButton& button, const juce::String& tip)
    {
        setTip (button, tip);
        button.setLookAndFeel (&customLookAndFeel);
        addAndMakeVisible (button);
    };

    // ---------------------------------------------------------------
    //  User presets: the combo lists what is on disk, SAVE opens a name
    //  dialog and DEL removes the selected file. Every action refreshes
    //  the list so two instances of the plugin stay honest with each
    //  other about what exists.
    // ---------------------------------------------------------------
    refreshUserPresetList();
    setTip (userPresetBox, "User presets - your own saved machine states. "
                              "Selecting one recalls it as a single undoable step.");
    userPresetBox.setLookAndFeel (&customLookAndFeel);
    userPresetBox.setTextWhenNothingSelected ("USER...");
    userPresetBox.onChange = [this]
    {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText (selectedId - 1);
        if (name.isEmpty() || name == audioProcessor.getCurrentPresetName())
            return;
        if (audioProcessor.applyUserPreset (name))
        {
            lastShownUserPreset = name;
            lastShownPreset = -1;
            // A user preset clears the factory selection: ID 0 is "no selection".
            presetBox.setSelectedId (audioProcessor.getLastPresetIndex() + 1,
                                     juce::dontSendNotification);
            updateWorkflowButtons();
        }
        else
        {
            refreshUserPresetList(); // the file vanished; rebuild the list
        }
    };
    addAndMakeVisible (userPresetBox);

    workflowButtonSetup (savePresetButton, "Store the whole machine state as a user "
                                           "preset you can recall in any session.");
    workflowButtonSetup (deletePresetButton, "Delete the selected user preset file.");
    savePresetButton.onClick = [this]
    {
        if (savePresetWindow != nullptr)
        {
            savePresetWindow->exitModalState (0);
            savePresetWindow.reset();
        }

        auto* window = new juce::AlertWindow ("Save user preset",
                                              "Name this machine state:",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("preset_name", audioProcessor.getCurrentPresetName(), "Name:");
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        savePresetWindow.reset (window);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            // Guard on the pointer: an older dialog's asynchronous callback must
            // never dismiss a newer one.
            if (savePresetWindow.get() != window)
                return;
            const auto chosen = window->getTextEditorContents ("preset_name");
            savePresetWindow.reset();
            if (result == 1 && chosen.trim().isNotEmpty())
            {
                if (audioProcessor.saveUserPreset (chosen))
                    refreshUserPresetList();
            }
        }), false);
    };
    deletePresetButton.onClick = [this]
    {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText (selectedId - 1);
        if (name.isNotEmpty() && audioProcessor.deleteUserPreset (name))
            refreshUserPresetList();
    };

    styleLabel (presetBadgeLabel, "", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    addAndMakeVisible (presetBadgeLabel);

    workflowButtonSetup (copyAButton, "Store the current settings in slot A. "
                                      "A is the side the plugin starts on.");
    workflowButtonSetup (copyBButton, "Store the current settings in slot B.");
    workflowButtonSetup (compareButton, "Toggle between slots A and B to compare settings "
                                        "with identical level, bypass state and oversampling.");
    workflowButtonSetup (undoButton, "Undo the last preset or A/B change (Ctrl+Z).");
    workflowButtonSetup (redoButton, "Redo an undone preset or A/B change (Ctrl+Y).");

    copyAButton.onClick = [this]
    {
        audioProcessor.copyToCompareSlot (0);
        updateWorkflowButtons();
    };
    copyBButton.onClick = [this]
    {
        audioProcessor.copyToCompareSlot (1);
        updateWorkflowButtons();
    };
    compareButton.onClick = [this]
    {
        audioProcessor.toggleCompare();
        updateWorkflowButtons();
    };
    undoButton.onClick = [this]
    {
        audioProcessor.getUndoManager().undo();
        updateWorkflowButtons();
    };
    redoButton.onClick = [this]
    {
        audioProcessor.getUndoManager().redo();
        updateWorkflowButtons();
    };

    styleLabel (compareBadgeLabel, "", 8.5f, paletteFor (false).accent,
                true, juce::Justification::centred);
    addAndMakeVisible (compareBadgeLabel);

    addAndMakeVisible (tapeTypeBox);
    addAndMakeVisible (valveTypeBox);
    addAndMakeVisible (ampTypeBox);
    addAndMakeVisible (transformerTypeBox);
    addAndMakeVisible (digitalTypeBox);
    addAndMakeVisible (vinylTypeBox);
    addAndMakeVisible (vinylSpeedBox);
    addAndMakeVisible (speedBox);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (themeButton);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tape_type", tapeTypeBox);
    valveTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "valve_type", valveTypeBox);
    ampTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "amp_type", ampTypeBox);
    transformerTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "transformer_type", transformerTypeBox);
    digitalTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "digital_type", digitalTypeBox);
    vinylTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_type", vinylTypeBox);
    vinylSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_speed", vinylSpeedBox);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "speed", speedBox);

    // The 3D transport goes in FIRST, so it is the bottom-most child. It is a
    // child component rather than part of this editor's own painting because it
    // needs a custom renderer and this editor's context is a component painter -
    // see Source/GUI/TapeScene.h. It sits in the strip resized() keeps clear of
    // every control, so nothing here can end up drawn on top of it.
    addAndMakeVisible (tapeScene);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (compressorMeterIn);
    addAndMakeVisible (compressorMeterOut);
    applyTheme();

    // OpenGL is only an optimisation for the animated panel, and it is the least
    // portable part of the UI: some Windows drivers, remote sessions, virtual machines
    // and headless hosts cannot create a context at all. Attaching unconditionally
    // means those setups get a broken or blank editor, so the context is treated as a
    // best-effort accelerator. If it cannot attach, the component renderer draws the
    // same panel through the software path with no visual difference.
    openGLContext.setComponentPaintingEnabled (true);
    openGLContext.setContinuousRepainting (false);

    // attachTo() returns void, so it cannot be tested directly - it would read as
    // `if (!void)`, which is not a valid expression. The documented way to find out whether
    // the context came up is to ask afterwards, and detach if it did not so the component
    // renderer draws the panel instead.
    openGLContext.attachTo (*this);

    if (! openGLContext.isAttached())
        openGLContext.detach();

    // The GL switch mirrors the context's real state rather than a wish: on
    // machines where the context never came up the button reads OFF and the
    // software renderer is what the user sees. The 3D transport follows the same
    // switch, because it is an accelerator rather than a feature.
    syncGlSwitchState();

    // OpenGL is ON by default, and a failed first attach is usually not a final
    // answer: the host may not have created the editor's native peer yet, and there
    // is no context to attach to without one. The timer keeps trying for a few
    // seconds and then gives up, because a driver, a remote session or a VM that
    // cannot create a context will not manage on the last attempt either.
    glAttachAttemptsLeft = openGLContext.isAttached() ? 0 : 90;
    glButton.onClick = [this]
    {
        // Whatever the user asked for wins: stop the startup retries, or they would
        // switch the accelerator back on seconds after the user turned it off.
        glAttachAttemptsLeft = 0;

        if (openGLContext.isAttached())
            openGLContext.detach();
        else
            openGLContext.attachTo (*this);

        // One place decides what the switch says, how it looks, and whether the
        // 3D transport gets a context, and all three read the context's state
        // afterwards: attachTo() can fail on a driver, a remote session or a VM,
        // and a branch that assumed success would leave the button claiming GL is
        // on while the software renderer is drawing the panel.
        syncGlSwitchState();
    };

    // The interface sounds are attached LAST, after every control they watch
    // exists AND after every existing onClick handler has been installed - so a
    // control's own handler is always the one that runs first and the sound
    // cannot replace it. See attachInterfaceSounds() for how each control's
    // voice is chosen.
    attachInterfaceSounds();

    startTimerHz (30);
}

FirstAudioProcessorEditor::~FirstAudioProcessorEditor()
{
    stopTimer();
    openGLContext.detach();

    for (auto& slider : controls)
        slider.setLookAndFeel (nullptr);

    tapeTypeBox.setLookAndFeel (nullptr);
    valveTypeBox.setLookAndFeel (nullptr);
    ampTypeBox.setLookAndFeel (nullptr);
    transformerTypeBox.setLookAndFeel (nullptr);
    digitalTypeBox.setLookAndFeel (nullptr);
    vinylTypeBox.setLookAndFeel (nullptr);
    vinylSpeedBox.setLookAndFeel (nullptr);
    speedBox.setLookAndFeel (nullptr);
    bypassButton.setLookAndFeel (nullptr);
    deltaButton.setLookAndFeel (nullptr);
    themeButton.setLookAndFeel (nullptr);
    presetBox.setLookAndFeel (nullptr);
    copyAButton.setLookAndFeel (nullptr);
    copyBButton.setLookAndFeel (nullptr);
    compareButton.setLookAndFeel (nullptr);
    undoButton.setLookAndFeel (nullptr);
    redoButton.setLookAndFeel (nullptr);
    polarityButton.setLookAndFeel (nullptr);
    autoGainButton.setLookAndFeel (nullptr);
    oversamplingBox.setLookAndFeel (nullptr);
    instrumentBox.setLookAndFeel (nullptr);
    vinylGenerationBox.setLookAndFeel (nullptr);
    vinylTurntableBox.setLookAndFeel (nullptr);
    vinylCartridgeBox.setLookAndFeel (nullptr);
    uiSoundsButton.setLookAndFeel (nullptr);
    inputEqHpButton.setLookAndFeel (nullptr);
    inputEqLpButton.setLookAndFeel (nullptr);
    outputEqHpButton.setLookAndFeel (nullptr);
    outputEqLpButton.setLookAndFeel (nullptr);
    transportStopButton.setLookAndFeel (nullptr);
    transportPlayButton.setLookAndFeel (nullptr);
    transportStartButton.setLookAndFeel (nullptr);
    spindownButton.setLookAndFeel (nullptr);
    delayTypeBox.setLookAndFeel (nullptr);
    delayRateBox.setLookAndFeel (nullptr);
    delaySyncButton.setLookAndFeel (nullptr);
    modeCycleButton.setLookAndFeel (nullptr);
    glButton.setLookAndFeel (nullptr);
    savePresetButton.setLookAndFeel (nullptr);
    deletePresetButton.setLookAndFeel (nullptr);

    if (savePresetWindow != nullptr)
        savePresetWindow->exitModalState (0);
    savePresetWindow.reset();
#if JUCE_DEBUG
    // Release the inspector before the components it watches are destroyed.
    inspector.reset();
#endif

}

void FirstAudioProcessorEditor::refreshUserPresetList()
{
    userPresetBox.clear (juce::dontSendNotification);
    const auto names = audioProcessor.getUserPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        userPresetBox.addItem (names[i], i + 1);

    const auto loaded = audioProcessor.getCurrentPresetName();
    const auto index = names.indexOf (loaded);
    userPresetBox.setSelectedItemIndex (index >= 0 ? index : -1, juce::dontSendNotification);
    lastShownUserPreset = index >= 0 ? loaded : juce::String();
}

void FirstAudioProcessorEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto names = FirstAudioProcessor::getPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);

    // A fresh instance has NO preset loaded (index -1): showing "Default" as the
    // box's selection then was a lie - the panel held the raw parameter defaults
    // (DRIVE 0.30), not that preset's values (DRIVE 0.28), and picking "Default"
    // looked like it had done nothing. Nothing selected is the honest display; the
    // first honest load happens when the user (or a session) actually applies one.
    const auto loaded = audioProcessor.getLastPresetIndex();
    presetBox.setSelectedItemIndex (loaded >= 0 ? loaded : -1, juce::dontSendNotification);
    lastShownPreset = loaded;
}

void FirstAudioProcessorEditor::updateWorkflowButtons()
{
    // The A/B buttons carry the active side on their text, so the panel always says
    // which slot is live without needing a separate readout.
    const auto activeSlot = audioProcessor.getActiveCompareSlot();
    copyAButton.setButtonText (activeSlot == 0 ? "A (LIVE)" : "COPY A");
    copyBButton.setButtonText (activeSlot == 1 ? "B (LIVE)" : "COPY B");
    compareButton.setButtonText (activeSlot == 0 ? "A/B: B" : "A/B: A");

    // An edited dot on the inactive slot's caption: when the two stored slots differ,
    // the side you are NOT hearing has something different on it.
    const auto dirty = audioProcessor.isCompareDirty();
    copyAButton.setButtonText (copyAButton.getButtonText()
                               + (dirty && activeSlot != 0 ? " *" : ""));
    copyBButton.setButtonText (copyBButton.getButtonText()
                               + (dirty && activeSlot != 1 ? " *" : ""));


    auto& undoManager = audioProcessor.getUndoManager();
    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());

    // The toggles draw themselves through J37LookAndFeel::drawToggleButton, which
    // reads the palette live, so theme and state changes recolour on the same frame
    // without any per-colour bookkeeping here.

    // Short captions: the badge sits at the right end of the preset row, where the
    // old "A/B MATCHED" could push into the redo button on a narrow panel.
    compareBadgeLabel.setText (dirty ? "EDITED" : "MATCHED",
                               juce::dontSendNotification);

    const auto presetName = audioProcessor.getCurrentPresetName();
    const auto presetIsDirty = audioProcessor.isPresetDirty();
    const auto presetBadgeText = presetName.isEmpty()
                                ? "FACTORY STATE"
                                : (presetIsDirty ? "PRESET: " + presetName + " (EDITED)"
                                                 : "PRESET: " + presetName);
    presetBadgeLabel.setText (presetBadgeText, juce::dontSendNotification);
    // Re-fit on every caption change, not just on resize: a user can name a preset
    // anything at all, and the badge must shrink to fit rather than clip at the edge.
    presetBadgeLabel.setFont (shrinkingFont (presetBadgeText, 8.0f, juce::Font::plain,
                                             static_cast<float> (presetBadgeLabel.getWidth()) - 4.0f));
    presetBadgeLabel.setColour (juce::Label::textColourId,
                                presetIsDirty ? paletteFor (darkTheme).accent
                                              : paletteFor (darkTheme).secondary);
}

bool FirstAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Ctrl/Cmd+Z and Ctrl/Cmd+Y mirror the DAW convention inside the plugin's own
    // preset/A/B history. The Shift+Z (redo) test comes FIRST: a bare Ctrl+Z also
    // matches the Shift variant, so testing undo first would swallow the redo chord.
    const auto commandDown = key.getModifiers().isCommandDown();
    if (commandDown && key.isKeyCode ('Y'))
    {
        audioProcessor.getUndoManager().redo();
        updateWorkflowButtons();
        return true;
    }
    if (commandDown && key.isKeyCode ('Z'))
    {
        if (key.getModifiers().isShiftDown())
            audioProcessor.getUndoManager().redo();
        else
            audioProcessor.getUndoManager().undo();
        updateWorkflowButtons();
        return true;
    }

    // 1 / 2 / 3 switch knob-grid tabs, the way any other plugin pages its controls.
    // This has to sit BEFORE the fall-through return: it was added after it, which left
    // the whole block unreachable and the digits doing nothing at all. Clang reported it
    // as -Wunreachable-code on the macOS build, which is how it was noticed. Tested
    // after the undo chords, and only without Shift, so a capitalised digit typed into
    // a slider's editable text box is still left alone.
    if (! commandDown && ! key.getModifiers().isShiftDown())
        for (int tab = 0; tab < numTabs; ++tab)
            if (key.getKeyCode() == '1' + tab)
            {
                setCurrentTab (tab);
                return true;
            }

    return Component::keyPressed (key);
}

FirstAudioProcessorEditor::EditorLayout FirstAudioProcessorEditor::getEditorLayout() const
{
    // Outer padding of 14 px instead of 18: the screws and the panel border only need
    // that much clearance, and every pixel saved here goes to the working areas.
    auto remaining = getLocalBounds().reduced (14);
    EditorLayout layout;

    // Header and deck are fixed-height, so they keep their proportions at small panel
    // sizes and on high-DPI displays. The header is 84 px - tall enough for three
    // right-hand rows: the four engine switches (BYPASS / DELTA / POLARITY / AUTO GAIN),
    // THEME with the language pair, and the machine-state readout on a full-width
    // band of its own (see the header grid in resized()).
    //
    // The deck is 278 px and was 322. The forty-four came out of the machine readouts,
    // which were a three-high vertical stack pinned to the deck's right edge and are
    // now three caption-over-value pairs side by side inside the 32 px band they share
    // with OVER, INSTRUMENT and GL. Nothing was moved off the deck to buy it - the
    // stack is the only thing that was taller than the band it was in - and the deck
    // keeps one band per group, so no two of them share a row.
    //
    // It is the same forty-four that comes off the editor's minimum height, which is
    // the difference between a window that fits a 768 px laptop and one that does
    // not. The floor is not cosmetic: it is what the host sizes the window to and
    // what the user cannot drag below.
    layout.header = remaining.removeFromTop (84);
    remaining.removeFromTop (8);

    // The deck's height is the one number in this function that was still fixed
    // when everything else had learned to flex, and it was the reason a short
    // window looked broken rather than merely tight.
    //
    // The control grid below divides whatever it is given (grid.getHeight() /
    // gridRows), so it copes with being squeezed. The deck could not: its six
    // bands sit at absolute offsets inside a rectangle of exactly 278 px, so on
    // a 1366x768 laptop - where the window has to be clamped to about 664 px to
    // stay on the screen at all - the deck kept all 278 of them and the grid got
    // what was left, roughly 258 px for three rows of knobs and a 2x2 meter
    // block. The panel did not fit because the part of it that had to give was
    // the part that could not.
    //
    // So the deck now flexes between a floor and its preferred height, and the
    // grid is guaranteed its minimum FIRST - the order matters, because taking
    // the grid's minimum off the top is what stops the two bands from both
    // ending up squeezed.
    //
    // 236 is the deck at its tightest, and it is a number rather than a guess:
    // the six bands hold 216 px of content and start 6 px down, so 222 px is
    // what they need before any gap at all. The 14 px that is left over is six
    // gaps of two - which is why the gap floor below is 2 and not 4. A floor of
    // 4 would need 246 px, and asking for 236 against it is a deck whose badge
    // gets drawn 10 px outside its own bottom edge.
    constexpr int deckPreferredHeight = 278;
    constexpr int deckMinimumHeight = 236;
    constexpr int controlsMinimumHeight = 300;
    constexpr int deckToControlsGap = 8;

    const auto availableForDeck = juce::jmax (0, remaining.getHeight() - deckToControlsGap);

    // jlimit keeps this ordered even when the window is shorter than the two
    // minimums together, which is exactly when a naive min/max would invert and
    // hand removeFromTop a negative height.
    const auto wantedDeckHeight = juce::jlimit (deckMinimumHeight, deckPreferredHeight,
                                                availableForDeck - controlsMinimumHeight);

    layout.deck = remaining.removeFromTop (juce::jmin (availableForDeck, wantedDeckHeight));
    remaining.removeFromTop (deckToControlsGap);

    // The meters panel has to hold a 2 x 2 grid of dials, so it claims a share of the
    // width rather than a fixed pixel count. That keeps both rows legible whether the
    // editor is at its minimum size or opened large.
    const auto metersWidth = juce::jlimit (320, 460, juce::roundToInt (static_cast<float> (remaining.getWidth()) * 0.30f));
    layout.meters = remaining.removeFromRight (metersWidth);
    remaining.removeFromRight (14);
    layout.controls = remaining;

    return layout;
}

void FirstAudioProcessorEditor::applyTheme()
{
    // The editor's own bool is derived from the three-way choice, so every one of
    // the ~50 `paletteFor (darkTheme)` call sites in this file keeps working
    // unchanged - and the two dark themes genuinely DO share all of them. The
    // only place the two are told apart is this next line.
    darkTheme = themeChoice != J37LookAndFeel::ThemeChoice::ivory;

    liveThemeChoice = themeChoice;

    const auto& palette = paletteForTheme (themeChoice);
    customLookAndFeel.setTheme (themeChoice);
    // The in-tab look and feel carries its own theme for the same reason the
    // deck's does: it reads the palette live at paint time, so the lists and the
    // pill switches recolour on the same frame as everything else.
    inlineLookAndFeel.setTheme (themeChoice);
    inputMeter.setDarkTheme (darkTheme);
    outputMeter.setDarkTheme (darkTheme);

    // The 3D transport reads the same palette rather than keeping colours of its
    // own, so it follows a theme switch on the same frame as everything else.
    // The four are chosen for what each one IS rather than for which palette slot
    // it resembles: the window is the panel's dead-black readout recess, the reel
    // is its machined plate, the highlight and the head are the accent (so the
    // specular reads as the machine's one indicator lamp), and the tape is the
    // knob face warmed and desaturated - a roll of tape, not a second accent.
    tapeScene.setPalette (palette.readout,
                          palette.raised,
                          palette.accent,
                          palette.knobFace.brighter (0.28f).withSaturation (0.55f));
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        for (std::size_t i = 0; i < decorativeOrbCount; ++i)
        {
            const auto orbColour = palette.accent.withAlpha (0.25f + static_cast<float> (i) * 0.04f);
            physicsOrbs[i].colour = orbColour;
            renderOrbs[i].colour = orbColour;
        }
    }

    brandLabel.setColour (juce::Label::textColourId, palette.accent);
    titleLabel.setColour (juce::Label::textColourId, palette.text);
    subtitleLabel.setColour (juce::Label::textColourId, palette.secondary);
    statusLabel.setColour (juce::Label::textColourId, palette.status);

    // The texture's doses are theme-dependent (light ink needs more), so a
    // theme flip recomputes them for BOTH renderers before the static layers
    // are rebuilt - otherwise a light panel would keep the dark theme's
    // weights until the next timer tick pushed new ones in.
    configureTextureWeights (panelTextureDrive, panelTextureGainReduction);
    deckHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    buildLabel.setColour (juce::Label::textColourId, palette.secondary);
    tapeTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    speedLabel.setColour (juce::Label::textColourId, palette.secondary);
    deckHintLabel.setColour (juce::Label::textColourId, palette.secondary);
    controlsHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    controlsHintLabel.setColour (juce::Label::textColourId, palette.secondary);
    metersHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    metersHintLabel.setColour (juce::Label::textColourId, palette.secondary);

    for (auto& label : controlLabels)
        label.setColour (juce::Label::textColourId, palette.secondary);

    // The deck's and the tabs' remaining captions. Every label the constructor
    // styles with paletteFor(false) has to be re-inked here as well, or a theme
    // switch leaves it in the previous panel's colour - the six type selectors,
    // the machine readouts and the tab-member captions all kept ivory-brown text
    // on the charcoal and metal panels until this block existed.
    languageLabel.setColour (juce::Label::textColourId, palette.secondary);
    bpmLabel.setColour (juce::Label::textColourId, palette.secondary);
    bpmReadout.setColour (juce::Label::textColourId, palette.secondary);
    vinylTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylSpeedLabel.setColour (juce::Label::textColourId, palette.secondary);
    valveTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    ampTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    transformerTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    digitalTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    diPadLabel.setColour (juce::Label::textColourId, palette.secondary);
    tracksLabel.setColour (juce::Label::textColourId, palette.secondary);
    inputEqOrderLabel.setColour (juce::Label::textColourId, palette.secondary);
    outputEqOrderLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylGenerationLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylTurntableLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylCartridgeLabel.setColour (juce::Label::textColourId, palette.secondary);
    delaySyncLabel.setColour (juce::Label::textColourId, palette.secondary);
    glLabel.setColour (juce::Label::textColourId, palette.secondary);
    neuralStatusLabel.setColour (juce::Label::textColourId, palette.secondary);

    // The tab buttons carry the theme, and styleTabButtons() reads currentTab as well,
    // so the selected tab keeps its accent highlight across a theme switch.
    styleTabButtons();

    for (auto& slider : controls)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, palette.text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, palette.readout);
        slider.setColour (juce::Slider::textBoxOutlineColourId, palette.border);
        slider.setColour (juce::Slider::thumbColourId, palette.accent);
    }

    const auto styleCombo = [&palette] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, palette.raised);
        box.setColour (juce::ComboBox::outlineColourId, palette.border);
        box.setColour (juce::ComboBox::textColourId, palette.text);
        box.setColour (juce::ComboBox::arrowColourId, palette.accent);
        box.setColour (juce::ComboBox::focusedOutlineColourId, palette.accent);
    };
    styleCombo (tapeTypeBox);
    styleCombo (valveTypeBox);
    styleCombo (ampTypeBox);
    styleCombo (transformerTypeBox);
    styleCombo (digitalTypeBox);
    styleCombo (vinylTypeBox);
    styleCombo (vinylSpeedBox);
    styleCombo (speedBox);
    styleCombo (presetBox);
    // The user-preset combo is the seventh ComboBox and was the one left out of this
    // list - and of the one in applyTheme() below - so it kept LookAndFeel_V4's light
    // default background on a dark panel and never followed the theme toggle.
    styleCombo (userPresetBox);
    // The language selector is drawn by the deck Look and Feel (which overrides
    // captions, not combo backgrounds), so like the user-preset list it needs its
    // palette colours set here or it keeps the base Look and Feel's own grey -
    // a box that matched nothing else on the panel.
    styleCombo (languageBox);

    // The lists' popup menus paint through the LookAndFeel_V4 item drawing, which
    // reads THESE colour ids off the attached LookAndFeel - at theme time there is
    // no PopupMenu object to style directly, so both LookAndFeels carry the palette
    // and whichever box opens a menu uses its own. A dark panel without them opened
    // the base look's light menu, whose black text on the menu's black selection
    // highlight was unreadable (the complaint behind "text is not readable").
    const auto stylePopupMenu = [&palette] (juce::LookAndFeel& lf)
    {
        lf.setColour (juce::PopupMenu::backgroundColourId, palette.panel.brighter (0.08f));
        lf.setColour (juce::PopupMenu::textColourId, palette.text);
        lf.setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.accent.withAlpha (0.85f));
        lf.setColour (juce::PopupMenu::highlightedTextColourId, palette.readout);
    };
    stylePopupMenu (customLookAndFeel);
    stylePopupMenu (inlineLookAndFeel);

    const auto styleWorkflowButton = [&palette] (juce::TextButton& button, bool emphasised)
    {
        button.setColour (juce::TextButton::buttonColourId, palette.raised);
        button.setColour (juce::TextButton::buttonOnColourId, palette.accent);
        button.setColour (juce::TextButton::textColourOffId, emphasised ? palette.text : palette.secondary);
        button.setColour (juce::TextButton::textColourOnId, palette.readout);
    };

    // bypassButton is a ToggleButton drawn by J37LookAndFeel::drawToggleButton.
    themeButton.setColour (juce::TextButton::buttonColourId, palette.raised);
    themeButton.setColour (juce::TextButton::textColourOffId, palette.text);

    styleCombo (oversamplingBox);
    styleCombo (instrumentBox);
    // The seven tab-member lists deliberately do NOT go through styleCombo: they
    // are drawn by J37InlineLookAndFeel, which reads the palette live and has
    // its own, flatter treatment. Styling their field would fight it. The VALUE
    // colour and font below are not part of that treatment either - juce::ComboBox
    // paints its value with an internal Label reading ComboBox::textColourId, and
    // an unstyled list kept the base LookAndFeel's BLACK value on the dark panels:
    // DI PAD, TRACKS, ORDER and the vinyl trio were simply unreadable. The font is
    // set through the same Label, so the in-tab lists use the flat field's own
    // compact size instead of whatever V4 scales to the box.
    const auto styleInlineListValue = [&palette] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::textColourId, palette.text);
    };
    styleInlineListValue (diPadBox);
    styleInlineListValue (tracksBox);
    styleInlineListValue (inputEqOrderBox);
    styleInlineListValue (outputEqOrderBox);
    styleInlineListValue (vinylGenerationBox);
    styleInlineListValue (vinylTurntableBox);
    styleInlineListValue (vinylCartridgeBox);
    // The delay trio is on the deck Look and Feel (see its construction), so it
    // follows styleCombo like every other deck list.
    styleCombo (delayTypeBox);
    styleCombo (delayRateBox);
    // The transport keys and the spindown button are TextButtons, not combos, so
    // they take their colours from styleTransportButtons() / styleSpindownButton(),
    // which read the palette AND the live state - that is what makes the active key
    // stay highlighted across a theme switch.
    styleTransportButtons();
    styleSpindownButton();
    // modeCycleButton is a TextButton whose colours styleWorkflowButton-style
    // calls do not own; it reads the palette through the look and feel, and its
    // caption is refreshed by refreshModeButtonCaption() below.
    styleCombo (userPresetBox);
    styleGlButton (openGLContext.isAttached());
    styleWorkflowButton (copyAButton, audioProcessor.getActiveCompareSlot() == 0);
    styleWorkflowButton (copyBButton, audioProcessor.getActiveCompareSlot() == 1);
    styleWorkflowButton (compareButton, false);
    styleWorkflowButton (undoButton, false);
    styleWorkflowButton (redoButton, false);
    styleWorkflowButton (savePresetButton, false);
    styleWorkflowButton (deletePresetButton, false);
    // polarityButton and autoGainButton are drawn by J37LookAndFeel::drawToggleButton
    // and need no per-theme colour calls here.
    presetHeadingLabel.setColour (juce::Label::textColourId, palette.secondary);
    oversamplingLabel.setColour (juce::Label::textColourId, palette.secondary);
    instrumentLabel.setColour (juce::Label::textColourId, palette.secondary);
    transportLabel.setColour (juce::Label::textColourId, palette.secondary);
    spindownLabel.setColour (juce::Label::textColourId, palette.secondary);
    delayTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    delayRateLabel.setColour (juce::Label::textColourId, palette.secondary);
    compareBadgeLabel.setColour (juce::Label::textColourId,
                                 audioProcessor.isCompareDirty() ? palette.accent : palette.secondary);

    compressorLabel.setColour (juce::Label::textColourId, palette.accent);
    compressorReadout.setColour (juce::Label::textColourId, palette.secondary);
    harmonicsLabel.setColour (juce::Label::textColourId, palette.accent);
    harmonicsReadout.setColour (juce::Label::textColourId, palette.secondary);
    subfundLabel.setColour (juce::Label::textColourId, palette.accent);
    subfundReadout.setColour (juce::Label::textColourId, palette.secondary);
    antiPhaseLabel.setColour (juce::Label::textColourId, palette.accent);
    antiPhaseReadout.setColour (juce::Label::textColourId, palette.secondary);
    compressorMeterIn.setDarkTheme (darkTheme);
    compressorMeterOut.setDarkTheme (darkTheme);

    // The caption names the theme you are ON, and the tooltip says what the next
    // press will do - so the label can never be read as "press this to become X"
    // when it is already X.
    switch (themeChoice)
    {
        case J37LookAndFeel::ThemeChoice::ivory:
            themeButton.setButtonText ("IVORY");
            break;
        case J37LookAndFeel::ThemeChoice::charcoal:
            themeButton.setButtonText ("CHARCOAL");
            break;
        case J37LookAndFeel::ThemeChoice::metal:
        default:
            themeButton.setButtonText ("METAL");
            break;
    }

    updateWorkflowButtons();
    // The mode button's colours read the palette (see refreshModeButtonCaption),
    // so a theme switch must re-run it - the caption is unchanged, the ink is not.
    refreshModeButtonCaption();
    repaint();
}

//==============================================================================
void FirstAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);

    // The band this paint actually has to cover. The panel is repainted in bands
    // (see timerCallback) and most of what this function draws never changes, so
    // the clip is used to skip whole sections instead of drawing them and letting
    // the rasteriser discard the pixels - the draw calls, the geometry and the
    // trigonometry are all still paid for if they are issued.
    const auto clip = g.getClipBounds();
    const auto inClip = [&clip] (const juce::Rectangle<int>& area) { return area.intersects (clip); };

    g.fillAll (palette.background);

    const auto layout = getEditorLayout();
    // ------------------------------------------------------------------
    //  Chassis grain.
    //
    //  Drawn BEFORE the section panels so it sits on the chassis rather than over
    //  the controls - the old version painted its 18 rules across the whole
    //  window, which put lines through every panel and read as scratches rather
    //  than as a surface.
    //
    //  Two passes: a fine 3 px weave for the texture, and a much fainter 12 px
    //  one to break up the regularity. A single period reads as a pattern; two
    //  beating against each other read as brushed metal.
    // ------------------------------------------------------------------
    const auto chassis = getLocalBounds().reduced (8).toFloat();
    const auto grainAlpha = darkTheme ? 0.016f : 0.022f;
    const auto grainColour = palette.text.withAlpha (grainAlpha);

    // Two passes over the whole chassis is a few hundred rules, and the panels are
    // drawn over the top of them immediately afterwards - so most of them are
    // covered within the same paint. Skipping the ones the clip cannot see is what
    // keeps that affordable at 30 Hz with a context attached, where every one of
    // these repaints is a render of this component.
    // Float, to match the float y the loops below count with. getY()/getBottom()
    // are int, and the implicit conversions warned on every Clang build.
    const auto grainTop = static_cast<float> (clip.getY());
    const auto grainBottom = static_cast<float> (clip.getBottom());

    g.setColour (grainColour);
    for (float y = chassis.getY() + 2.0f; y < chassis.getBottom(); y += 3.0f)
        if (y >= grainTop && y <= grainBottom)
            g.drawHorizontalLine (juce::roundToInt (y),
                                  chassis.getX() + 6.0f, chassis.getRight() - 6.0f);

    g.setColour (palette.text.withAlpha (grainAlpha * 0.6f));
    for (float y = chassis.getY() + 7.0f; y < chassis.getBottom(); y += 12.0f)
        if (y >= grainTop && y <= grainBottom)
            g.drawHorizontalLine (juce::roundToInt (y),
                                  chassis.getX() + 6.0f, chassis.getRight() - 6.0f);

    // (The arithmetic texture is blitted further down, AFTER the panels, so the
    // panels' own faces sit over the chassis grain rather than over the texture.)


    drawPanel (g, getLocalBounds().reduced (8), palette, 7.0f);
    drawPanel (g, layout.header, palette, 5.0f);
    drawPanel (g, layout.deck, palette, 5.0f);
    drawPanel (g, layout.controls, palette, 5.0f);
    drawPanel (g, layout.meters, palette, 5.0f);

    // ----------------------------------------------------------------------
    //  The panel's texture, 2D and mathematical - the same three terms the GPU
    //  scene shades, in the same doses (uTextureWeights there):
    //
    //   - static grain + wear blotches: ONE image, built once per resize in
    //     rebuildPanelTextureLayers() from the same hash spaces the shader
    //     uses (grain = fract(sin(p*7.5)), wear = value noise), blitted here
    //     in one call - AFTER the panels, so their faces carry it rather than
    //     hiding it, and BEFORE the child controls, which paint on top of
    //     everything here by being children;
    //   - live shimmer: a sparse drift of dots whose positions are the shader's
    //     hash(face*40 + t*(1+GR)) evaluated at a walking time offset - so the
    //     surface visibly responds to DRIVE and to the compressors. THIS is
    //     what the GL switch switches between: with the context attached the
    //     scene's fragment shader draws the identical texture definition on
    //     the GPU; without it, this ink does.
    //  Every dose is centered, so no branch can shift the panel's colour.
    // ----------------------------------------------------------------------
    if (panelTextureImage.isValid())
    {
        // Two rules keep the surface from reading as "square blocks":
        //
        //  1. RESAMPLING QUALITY. The ten-argument drawImage carries no quality
        //     of its own - the blit runs at whatever Graphics::setImageResamplingQuality
        //     last set, and Graphics' default is LOW, i.e. nearest-neighbour:
        //     a half-res layer drawn through it arrives as 2x2 blocks, which is
        //     precisely the "quadrats" the user saw on alternating frames. The
        //     quality is set explicitly for this one blit and restored after, so
        //     every other image in the panel keeps its old speed.
        //
        //  2. OPACITY-EXACT. The layer was built with per-pixel alpha already
        //     carrying the dose, so multiplying it again would brighten the grain
        //     whenever a repaint had to re-blend the layer, and the texture would
        //     visibly jump between "normal" and "too strong" as frames
        //     alternated. The alpha in the image IS the final amount.
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (panelTextureImage,
                     0, 0, getWidth(), getHeight(),
                     0, 0, panelTextureImage.getWidth(), panelTextureImage.getHeight(),
                     false /* fillAlphaChannelWithCurrentBrush: NO - the alpha in
                              the image is the final dose, re-multiplying it with
                              a brush would double the ink */);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    }

    if (panelTextureWeights.shimmer > 0.0f)
    {
        const auto timeNow = static_cast<float> (juce::Time::getMillisecondCounterHiRes()) * 0.001f;
        // The shimmer drifts across the WHOLE panel: the timer now repaints
        // everything every tick, and the dots' hash chains are keyed to the
        // area they land in, so pinning them to the header+deck union while
        // the grain layer covered the whole editor read as two different
        // materials on one surface.
        const auto area = getLocalBounds().toFloat();
        const auto hashOf = [] (float x)
        {
            return std::fmod (std::abs (std::sin (x) * 43758.5453123f), 1.0f);
        };
        const auto timeStep = std::floor (timeNow * 8.0f);
        const auto drift = timeStep * 0.35f * (1.0f + panelTextureGainReduction);

        for (int i = 0; i < panelShimmerDotCount; ++i)
        {
            const auto fi = static_cast<float> (i);
            const auto u = hashOf (fi * 1.37f + drift);
            const auto v = hashOf (fi * 3.11f + drift * 0.73f + 19.7f);
            const auto a = hashOf (fi * 7.77f - drift * 1.31f + 5.3f);
            const auto bright = a > 0.5f;
            const auto ink = bright ? palette.text : palette.knobEdge;
            const auto alpha = panelTextureWeights.shimmer * (0.35f + 0.65f * std::abs (a - 0.5f) * 2.0f);

            g.setColour (ink.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
            g.fillEllipse (area.getX() + u * area.getWidth(),
                           area.getY() + v * area.getHeight(), 1.7f, 1.7f);
        }
    }

    g.setColour (palette.accent.withAlpha (0.85f));
    g.fillRect (layout.header.getX() + 17, layout.header.getY() + 14, 3,
                layout.header.getHeight() - 28);

    g.setColour (palette.border.withAlpha (0.75f));
    // Under the tab bar, not through it: the divider used to sit 42 px down, which is
    // where the first tab button now starts.
    g.drawHorizontalLine (layout.controls.getY() + controlsDividerOffset,
                          static_cast<float> (layout.controls.getX() + 18),
                          static_cast<float> (layout.controls.getRight() - 18));
    g.drawHorizontalLine (layout.meters.getY() + 48,
                          static_cast<float> (layout.meters.getX() + 18),
                          static_cast<float> (layout.meters.getRight() - 18));
    // The deck divider sits one pixel above the panel's bottom edge, BELOW the preset
    // badge band. It used to be eight pixels above the bottom, which put it straight
    // through the middle of the badge text at the minimum editor size.
    g.drawHorizontalLine (layout.deck.getBottom() - 1,
                          static_cast<float> (layout.deck.getX() + 16),
                          static_cast<float> (layout.deck.getRight() - 16));

    // The machine-state badge: a raised pill across the header grid's THIRD row,
    // wrapped around the statusLabel that lives there. The text moved to the
    // deck in an earlier pass but the drawn pill stayed in the header, so the
    // header switches printed over an empty badge plate - the user saw controls
    // on a design that had lost its text. The design now lives with its text
    // again, on a row nothing else occupies: the pill cannot be collided with.
    const auto statusPill = juce::Rectangle<float> (
        static_cast<float> (layout.header.getRight()
                                - (4 * 92 + 3 * 6)),
        static_cast<float> (layout.header.getY() + 62),
        static_cast<float> (4 * 92 + 3 * 6 - 18), 18.0f);
    g.setColour (palette.raised.darker (0.16f));
    g.fillRoundedRectangle (statusPill, 4.0f);

    // Status lamp: pulses with the compressor, so the panel shows that the
    // plugin is alive and working even when no gain reduction is happening.
    const auto lampCentre = juce::Point<float> (statusPill.getX() + 15.0f,
                                                statusPill.getCentreY());
    const auto lampPulse = 4.5f + 2.5f * glowAmount;
    g.setColour (palette.status.withAlpha (0.12f + 0.30f * glowAmount));
    g.fillEllipse (lampCentre.x - lampPulse * 2.2f, lampCentre.y - lampPulse * 2.2f,
                   lampPulse * 4.4f, lampPulse * 4.4f);
    g.setColour (palette.status);
    g.fillEllipse (lampCentre.x - 3.5f, lampCentre.y - 3.5f, 7.0f, 7.0f);

    // A hairline rule under the header strip separates the title block from the
    // controls; it runs the full width again - the badge it used to dodge lives
    // on the grid's third row now.
    g.setColour (palette.border.withAlpha (0.5f));
    g.drawHorizontalLine (layout.header.getBottom() - 4,
                          static_cast<float> (layout.header.getX() + 18),
                          static_cast<float> (layout.header.getRight() - 18));

    // getX()/getRight() return int, so the 9.0f insets are added *after* the cast.
    // `getX() + 9.0f` promotes the int to float implicitly, which is exactly what
    // the int-to-float conversion warnings on macOS flag. Reading the edges into
    // float locals once also keeps the four screw positions consistent with each
    // other instead of each one repeating its own mixed int/float arithmetic.
    const auto headerX = static_cast<float> (layout.header.getX());
    const auto headerRight = static_cast<float> (layout.header.getRight());
    const auto headerY = static_cast<float> (layout.header.getY());
    const auto headerBottom = static_cast<float> (layout.header.getBottom());

    for (const auto point : { juce::Point<float> (headerX + 9.0f, headerY + 9.0f),
                              juce::Point<float> (headerRight - 9.0f, headerY + 9.0f),
                              juce::Point<float> (headerX + 9.0f, headerBottom - 9.0f),
                              juce::Point<float> (headerRight - 9.0f, headerBottom - 9.0f) })
        drawScrew (g, point.x, point.y, palette);

    // The reel, the spokes, the tape ribbon and the orbs are the deck's, and they
    // are the most expensive thing in here: six spokes' worth of cos/sin, a path
    // stroke, and a copy of the particle array out from under its spin lock. They
    // are also the last thing this function draws, so when the clip is the header
    // band there is nothing after them to skip and this can simply stop.
    if (! inClip (layout.deck))
        return;

    // The reel and the ribbon below are the deck's SOFTWARE transport, and they are
    // drawn only when the 3D one is not. tapeScene.isSceneLive() is true only once
    // its context is up AND its shader linked, so a failed context, a driver that
    // will not oblige, and the user turning GL off all land back here - the panel
    // is never left with a hole where the transport was, and never shows two
    // transports at once. The particles further down are NOT gated: they live in
    // the gap the switches row left, a different part of the deck from the 3D
    // transport's window, so the two never overlap.
    //
    // The decorative reel lives in the deck's TOP-RIGHT corner, in the heading band
    // (y + 6..22) where no control sits, and clear of the harmonics readout, which is
    // right-aligned below it on the switches line. It used to sit ON the readout text.
    //
    // The reel is now a real transport indicator rather than decoration: its hub
    // dims as the platter slows, so a STOP empties it and a held SPINDOWN is
    // visible as the ring going grey before the spokes stop.
    if (! tapeScene.isSceneLive())
    {
        const auto reelCentre = juce::Point<float> (static_cast<float> (layout.deck.getRight() - 42),
                                                    static_cast<float> (layout.deck.getY() + 14));
        const auto platterNow = juce::jlimit (0.0f, 1.0f, smoothedMachineSpeed);
        g.setColour (palette.accent.withAlpha (0.10f + 0.30f * platterNow));
        for (const auto radius : { 11.0f, 7.0f, 2.0f })
            g.drawEllipse (reelCentre.x - radius, reelCentre.y - radius,
                           radius * 2.0f, radius * 2.0f, 1.0f);
        g.drawLine (reelCentre.x - 14.0f, reelCentre.y, reelCentre.x + 14.0f, reelCentre.y, 0.8f);
        g.drawLine (reelCentre.x, reelCentre.y - 14.0f, reelCentre.x, reelCentre.y + 14.0f, 0.8f);

        // Spokes: density tracks the selected tape speed, drift tracks the modulation,
        // and the whole set fades out as the platter coasts down - which is what makes a
        // spindown read as a turntable losing its drive rather than as a paused picture.
        const auto spokeAlpha = (0.20f + 0.45f * glowAmount) * (0.25f + 0.75f * platterNow);
        for (int spoke = 0; spoke < 6; ++spoke)
        {
            const auto spokeAngle = reelAngle + static_cast<float> (spoke)
                                                  * juce::MathConstants<float>::pi / 3.0f;
            const auto spokeInner = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 2.0f,
                                                        reelCentre.y + std::sin (spokeAngle) * 2.0f);
            const auto spokeOuter = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 10.5f,
                                                        reelCentre.y + std::sin (spokeAngle) * 10.5f);
            g.setColour (palette.accent.withAlpha (spokeAlpha));
            g.drawLine (spokeInner.x, spokeInner.y, spokeOuter.x, spokeOuter.y, 1.4f);

            g.setColour (palette.readout.withAlpha (0.10f + 0.25f * glowAmount));
            g.fillEllipse (spokeOuter.x - 1.6f, spokeOuter.y - 1.6f, 3.2f, 3.2f);
        }

        // Tape ribbon from the small reel down the deck's right edge, sagging with the
        // wow/flutter drift. It hangs from the reel at deck.right - 42, so the only row
        // that has to stay clear of it is the model row, and that row stops at the
        // standard right inset (deck.right - 14) - twenty-eight pixels to the right of
        // the ribbon's leftmost point. That is a fact about the ribbon, not a number
        // that has to be kept in step with the row's width by hand.
        const auto sag = (driftAmount - 0.5f) * 5.0f;
        juce::Path tapeRibbon;
        tapeRibbon.startNewSubPath (reelCentre.x, reelCentre.y + 12.0f);
        const auto deckY = static_cast<float> (layout.deck.getY());
        tapeRibbon.quadraticTo (reelCentre.x + 30.0f + sag,
                                deckY + 56.0f,
                                reelCentre.x + 6.0f + sag,
                                deckY + 60.0f);
        g.setColour (palette.knobEdge.withAlpha (0.35f));
        g.strokePath (tapeRibbon, juce::PathStrokeType (1.4f));
    } // ! tapeScene.isSceneLive()

    std::array<RenderOrb, decorativeOrbCount> orbsToDraw;
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        orbsToDraw = renderOrbs;
    }

    // The corridor the switches row left free, which resized() measured rather
    // than assumed - see deckParticleCorridorX. An empty range means the row had
    // no gap wide enough to be worth drawing into (a very narrow window), and
    // the right answer then is no particles at all: they are decoration, and
    // decoration on top of a control is the failure this whole pass is about.
    if (deckParticleCorridorX.getLength() < 20)
        return;

    for (const auto& orb : orbsToDraw)
    {
        const auto x = juce::jmap (orb.position.x, 0.0f, 1.5f,
                                   static_cast<float> (deckParticleCorridorX.getStart()) + 6.0f,
                                   static_cast<float> (deckParticleCorridorX.getEnd()) - 6.0f);
        const auto y = juce::jmap (orb.position.y, 0.0f, 1.2f,
                                   static_cast<float> (deckParticleCorridorY.getStart()),
                                   static_cast<float> (deckParticleCorridorY.getEnd()));
        const auto radius = juce::jmap (orb.radius, 0.045f, 0.057f, 2.0f, 3.5f);

        // Halos breathe so the orbs read as particles rather than static dots.
        const auto haloScale = 1.8f + 0.6f * std::sin (glowPhase + orb.position.x * 6.0f);
        g.setColour (orb.colour.withAlpha (0.07f + 0.05f * glowAmount));
        g.fillEllipse (x - radius * haloScale, y - radius * haloScale,
                       radius * haloScale * 2.0f, radius * haloScale * 2.0f);
        g.setColour (orb.colour.withAlpha (0.17f + 0.14f * glowAmount));
        g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void FirstAudioProcessorEditor::createDecorativePhysics()
{
    physicsWorld = std::make_unique<b2World> (b2Vec2 (0.0f, 0.0f));

    b2BodyDef boundaryDefinition;
    auto* boundaryBody = physicsWorld->CreateBody (&boundaryDefinition);

    const auto addWall = [boundaryBody] (float centreX, float centreY,
                                         float halfWidth, float halfHeight)
    {
        b2PolygonShape wallShape;
        wallShape.SetAsBox (halfWidth, halfHeight, b2Vec2 (centreX, centreY), 0.0f);

        b2FixtureDef wallFixture;
        wallFixture.shape = &wallShape;
        wallFixture.friction = 0.05f;
        wallFixture.restitution = 0.82f;
        boundaryBody->CreateFixture (&wallFixture);
    };

    addWall (0.75f, 0.0f, 0.75f, 0.02f);
    addWall (0.75f, 1.2f, 0.75f, 0.02f);
    addWall (0.0f, 0.6f, 0.02f, 0.6f);
    addWall (1.5f, 0.6f, 0.02f, 0.6f);

    for (std::size_t i = 0; i < decorativeOrbCount; ++i)
    {
        const auto index = static_cast<int> (i);
        const auto radius = 0.045f + static_cast<float> (index % 2) * 0.012f;
        const auto position = b2Vec2 (0.30f + static_cast<float> (index) * 0.18f,
                                      0.28f + static_cast<float> (index % 3) * 0.16f);

        b2BodyDef bodyDefinition;
        bodyDefinition.type = b2_dynamicBody;
        bodyDefinition.position = position;
        bodyDefinition.linearVelocity.Set ((index % 2 == 0 ? 1.0f : -1.0f) * 0.34f,
                                           (index % 3 == 0 ? 0.5f : -0.35f) * 0.42f);
        bodyDefinition.linearDamping = 0.04f;
        auto* body = physicsWorld->CreateBody (&bodyDefinition);

        b2CircleShape circleShape;
        circleShape.m_radius = radius;

        b2FixtureDef orbFixture;
        orbFixture.shape = &circleShape;
        orbFixture.density = 0.7f;
        orbFixture.friction = 0.15f;
        orbFixture.restitution = 0.82f;
        body->CreateFixture (&orbFixture);

        const auto colour = paletteFor (false).accent.withAlpha (
            0.25f + static_cast<float> (index) * 0.04f);
        physicsOrbs[i] = { body, radius, colour };
        renderOrbs[i] = { juce::Point<float> (position.x, position.y), radius, colour };
    }
}

void FirstAudioProcessorEditor::timerCallback()
{
    // The startup retry for the OpenGL context, bounded and then forgotten. It runs
    // first so a context that comes up is live before the meters ask for a repaint.
    if (glAttachAttemptsLeft > 0)
    {
        --glAttachAttemptsLeft;

        if (openGLContext.isAttached())
        {
            glAttachAttemptsLeft = 0;
        }
        else
        {
            openGLContext.attachTo (*this);

            if (openGLContext.isAttached())
            {
                glAttachAttemptsLeft = 0;
                syncGlSwitchState();
            }
        }
    }

    // Four meters: the two level meters each show the full four-way loudness reading
    // (peak dB, RMS, LUFS, VU and their equal-weighted combination), and each glue
    // compressor stage gets its own reduction meter fed from its own telemetry, so the
    // panel shows both what the signal level is doing and where the work is being done.
    //
    // The whole frame comes from ONE call. With readerwriterqueue compiled in, that
    // call drains the lock-free queue and hands back the newest COMPLETE frame the
    // audio thread published, so every number on the panel belongs to the same block
    // of audio - a new output peak can no longer be drawn beside the previous block's
    // RMS. Without the library the call assembles the same struct from the atomics,
    // so this code is identical either way.
    const auto telemetry = audioProcessor.getTelemetry();

    // The neural model readout is cheap to refresh and cheap to check: the
    // status only changes when a model is loaded or cleared, so the label is
    // only rewritten when its text would actually differ. That keeps a 30 Hz
    // timer from re-laying out a label sixty times a second for no reason.
    refreshNeuralStatus();

    inputMeter.setLoudness (telemetry.inputPeakDb,
                            telemetry.inputRmsDb,
                            telemetry.inputLufs,
                            telemetry.inputVuDb,
                            telemetry.inputCombinedDb,
                            telemetry.inputClipping);
    outputMeter.setLoudness (telemetry.outputPeakDb,
                             telemetry.outputRmsDb,
                             telemetry.outputLufs,
                             telemetry.outputVuDb,
                             telemetry.outputCombinedDb,
                             telemetry.outputClipping);

    compressorMeterIn.setReduction (telemetry.inputGainReductionDb,
                                    telemetry.inputCompressorActivity);
    compressorMeterOut.setReduction (telemetry.outputGainReductionDb,
                                     telemetry.outputCompressorActivity);

    const auto reduction = telemetry.inputGainReductionDb + telemetry.outputGainReductionDb;
    const auto activity = telemetry.compressorActivity;

    // Report the harmonic balance the tape stage is producing. Even and odd are shown
    // side by side because the ratio between them is the character: even-dominant reads as
    // warm and full, odd-dominant as hard and edgy, and real tape has both.
    const auto evenRatio = telemetry.evenHarmonicRatio;
    const auto oddRatio = telemetry.oddHarmonicRatio;
    harmonicsReadout.setText ("E " + juce::String (evenRatio * 100.0f, 1) + " %"
                                  + "   O " + juce::String (oddRatio * 100.0f, 1) + " %",
                              juce::dontSendNotification);

    // Colour the readout by which family dominates, so the character is readable at a
    // glance without needing to compare the numbers.
    const auto harmonicPalette = paletteFor (darkTheme);
    const auto totalHarmonics = evenRatio + oddRatio;
    harmonicsReadout.setColour (juce::Label::textColourId,
                                totalHarmonics < 0.001f ? harmonicPalette.secondary
                                : evenRatio >= oddRatio ? harmonicPalette.status
                                                        : harmonicPalette.needle);

    // The deck's tempo, as the host reports it. A host that offers no playhead
    // (an offline render, a bare player) leaves the readout at the machine's
    // default and says so, because a number that looks measured but is only a
    // fallback is worse than an honest dash.
    const auto deckBpm = audioProcessor.getDeckTempoBpm();
    const auto deckBpmValid = audioProcessor.getDeckTempoValid();
    bpmReadout.setText (deckBpmValid
                            ? juce::String (deckBpm, 1) + " " + xlat ("BPM")
                            : juce::String (juce::roundToInt (deckBpm)) + " "
                                  + xlat ("BPM") + " (" + xlat ("default") + ")",
                        juce::dontSendNotification);
    bpmReadout.setColour (juce::Label::textColourId,
                          deckBpmValid ? paletteFor (darkTheme).status
                                       : paletteFor (darkTheme).secondary);

    // ------------------------------------------------------------------
    //  Subharmonic tracking readout.
    //
    //  Reports the note the cascade is locked to and how solidly. This is the
    //  one piece of information the panel could not previously give: a depth
    //  knob at 60 % looks identical whether the generator is producing a clean
    //  undertone series or sitting idle because the detector never found a note
    //  to derive one from. The confidence is what distinguishes those two
    //  states, and the tracked frequency says WHICH note it locked to - which is
    //  how a user finds out that a mix is being read an octave low.
    // ------------------------------------------------------------------
    const auto trackedHz = telemetry.subfundTrackedHz;
    const auto confidence = telemetry.subfundConfidence;

    if (confidence < 0.05f || trackedHz <= 0.0f)
    {
        subfundReadout.setText (xlat ("idle - no note tracked"), juce::dontSendNotification);
        subfundReadout.setColour (juce::Label::textColourId, harmonicPalette.secondary);
    }
    else
    {
        // The confidence is shown as a percentage rather than hidden, because a
        // partial lock (say 40 %) is a real and useful state: the cascade IS
        // generating, but it is following a signal that is not steady enough for
        // a clean track, and the user should be able to see that.
        subfundReadout.setText (juce::String (trackedHz, 1) + " Hz   "
                                    + juce::String (juce::roundToInt (confidence * 100.0f)) + " %",
                                juce::dontSendNotification);
        // Green while the track is solid, amber while it is partial: the colour is
        // the readout's own confidence bar.
        subfundReadout.setColour (juce::Label::textColourId,
                                  confidence > 0.75f ? harmonicPalette.status
                                                     : harmonicPalette.needle);
    }

    // ------------------------------------------------------------------
    //  Anti-phase guard readout.
    //
    //  The guard's correction, as a percentage. "clean" is the healthy state and
    //  is what the panel says almost always; a rising number means the two sides
    //  were found to be in opposition and are being pulled back into agreement.
    //  It is shown rather than hidden because this is the one fault that is
    //  INVISIBLE in stereo: without the readout a user would never know the guard
    //  was doing anything, and with it they can see that a fault was caught.
    // ------------------------------------------------------------------
    const auto antiPhaseNow = telemetry.antiPhaseAmount;
    const auto antiPhaseText = antiPhaseNow < 0.01f
                                   ? xlat ("clean")
                                   : juce::String (juce::roundToInt (antiPhaseNow * 100.0f))
                                         + " " + xlat ("% corrected");
    if (antiPhaseText != lastShownAntiPhase)
    {
        lastShownAntiPhase = antiPhaseText;
        antiPhaseReadout.setText (antiPhaseText, juce::dontSendNotification);
        antiPhaseReadout.setColour (juce::Label::textColourId,
                                    antiPhaseNow < 0.01f ? harmonicPalette.status
                                                         : harmonicPalette.needle);
    }

    // Animated presentation state. Everything here is derived from audio
    // telemetry, so the panel visibly reacts to what the plugin is doing.
    glowPhase += 0.13f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
        glowPhase -= juce::MathConstants<float>::twoPi;

    const auto targetGlow = juce::jlimit (0.0f, 1.0f,
                                          activity * 0.7f + std::abs (reduction) / 6.0f);
    glowAmount += (targetGlow - glowAmount) * 0.18f;

    const auto drift = telemetry.transportDrift;
    driftAmount += (drift - driftAmount) * 0.25f;

    // The header status text follows the real bypass state of the processor.
    // Set UNCONDITIONALLY rather than on change: Label::setText early-outs on an
    // identical string, and the unconditional call is what makes a language
    // switch re-translate the readout on the very next frame without waiting
    // for a bypass toggle to change the state first.
    const auto bypassed = telemetry.bypassActive;
    currentBypassDisplay = bypassed;
    statusLabel.setText (bypassed ? xlat ("BYPASSED / DRY") : xlat ("STEREO / REAL TIME"),
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           bypassed ? paletteFor (darkTheme).secondary
                                    : paletteFor (darkTheme).status);

    // The reels spin at the machine's ACTUAL platter speed, which is the transport
    // ramp multiplied by the spindown ramp - so a START makes them visibly come up
    // to speed and a held SPINDOWN makes them coast down and stop. That is the
    // whole point of publishing getTransportRamp(): before this the reels only knew
    // the SPEED combo, so the transport did not move them at all.
    // getSelectedId() returns 0 while nothing is selected, which would index the
    // combo's item list at -1, so the index is clamped before it is used.
    const auto speedIndex = juce::jlimit (0, 2, speedBox.getSelectedId() - 1);
    const auto speedScale = speedIndex == 0 ? 0.55f : (speedIndex == 1 ? 0.85f : 1.25f);

    // Follow the machine's own platter with a display-only lag, so the reels ease
    // into a spindown rather than snapping when a block boundary lands.
    const auto machineSpeed = telemetry.transportRamp;
    smoothedMachineSpeed += (machineSpeed - smoothedMachineSpeed) * 0.20f;

    const auto targetReelSpeed = speedScale * (0.9f + driftAmount * 0.5f) * smoothedMachineSpeed;
    reelSpeed += (targetReelSpeed - reelSpeed) * 0.08f;
    reelAngle += reelSpeed * 0.09f;
    if (reelAngle > juce::MathConstants<float>::twoPi)
        reelAngle -= juce::MathConstants<float>::twoPi;

    // The spindown lamp follows both the parameter (so automation lights it) and
    // the button's own mouse state (so the lamp is instant on a press, before the
    // 30 Hz poll would see the parameter change).
    const auto transportNow = currentTransportState();
    const auto spindownNow = audioProcessor.isSpindownHeld()
                              || spindownButton.isDown();

    // The three transport keys relight only when the state actually changes.
    if (transportNow != lastShownTransport)
    {
        lastShownTransport = transportNow;
        styleTransportButtons();
    }

    if (spindownNow != lastShownSpindown)
    {
        lastShownSpindown = spindownNow;
        styleSpindownButton();
    }

    // The deck hint reads the machine's actual state rather than a fixed caption,
    // so the panel answers "what is the transport doing" in words as well as with
    // the key lamps and the reels. The four states are the ones the engine can be
    // in, and the spindown takes priority because it is the momentary one.
    const auto machineStateText = spindownNow ? "SPINDOWN / POWER CUT"
                                    : transportNow == 0 ? "STOPPED / AT REST"
                                    : transportNow == 2 ? "START / SPINNING UP"
                                                        : "PLAY / AT SPEED";
    // Explicit juce::String construction: comparing const char* against a
    // juce::String with != is ambiguous (candidates on both sides), so the
    // left operand is materialised first.
    // The mode caption rides the same refresh: any path that flips a mode
    // (session load, undo, preset, automation) shows up within one frame.
    refreshModeButtonCaption();

    if (juce::String (machineStateText) != lastShownMachineState)
    {
        lastShownMachineState = machineStateText;
        deckHintLabel.setText (machineStateText, juce::dontSendNotification);
        deckHintLabel.setColour (juce::Label::textColourId,
                                 spindownNow ? paletteFor (darkTheme).needle
                                 : transportNow == 0 ? paletteFor (darkTheme).secondary
                                                     : paletteFor (darkTheme).status);
    }

    customLookAndFeel.setActivity (glowAmount);
    customLookAndFeel.setDrift (driftAmount);
    customLookAndFeel.advanceFrame();

    // ------------------------------------------------------------------
    //  Interface sounds.
    //
    //  The enable flag follows the parameter, so the switch on the SETTINGS
    //  tab (and an automation lane, and a preset) all reach it by the same
    //  route. The gate counter is decremented once per frame here rather than
    //  being a wall-clock timestamp, which keeps the knob detent's rate tied to
    //  the panel's own frame rate instead of the system clock.
    //
    //  The brightness the sounds are pitched from is the machine's own activity
    //  - the same value the knob halos breathe with - so a machine doing work
    //  ticks at a slightly higher, tighter pitch than an idle one. That is the
    //  one refinement that makes the sounds belong to THIS plugin rather than
    //  being a generic UI beep.
    // ------------------------------------------------------------------
    if (uiSoundTickCountdown > 0)
        --uiSoundTickCountdown;

    const auto uiSoundEnabled = audioProcessor.getUiSoundsEnabled();
    if (uiSoundEnabled != lastShownUiSoundEnabled)
    {
        lastShownUiSoundEnabled = uiSoundEnabled;
        uiSounds.setEnabled (uiSoundEnabled);
    }

    uiSounds.setBrightness (juce::jlimit (0.0f, 1.0f, glowAmount * 0.7f + activity * 0.3f));

    // The WHOLE panel repaints on every tick now, and the reason is the
    // texture: the grain + wear layer spans the entire editor, and repainting
    // only the header + deck made the controls and meters sit on a texture
    // that was there on the frames their band happened to redraw and frozen on
    // the ones it did not - the user's "now it is normal, now it is squares,
    // now it is gone". A full-editor repaint at 30 Hz is the one schedule
    // under which the surface is uniform and its cost stays bounded.
    //
    // The control band used to be excluded on purpose: repaint() on a parent
    // never repaints its children in JUCE - the sliders are child components
    // and repaint themselves when their own value changes - so the old call
    // never reached a knob anyway. What it did redraw, thirty times a second,
    // was this component's own static content in that band: the panel gradient,
    // the grain and the divider. With a context attached, each of those is a
    // separate render of the panel, so it was roughly a third of the per-frame
    // work for a picture that never changed. The knobs look the same without
    // it, because they were never coming from here.
    repaint();

    // Workflow state is cheap to poll at 30 Hz and makes the panel self-healing:
    // if the host, a session load or a preset change moves anything, the buttons,
    // badge and preset display catch up on the next frame instead of lying.
    const auto canUndoNow = audioProcessor.getUndoManager().canUndo();
    const auto canRedoNow = audioProcessor.getUndoManager().canRedo();
    const auto slotNow = audioProcessor.getActiveCompareSlot();
    const auto dirtyNow = audioProcessor.isCompareDirty();
    const auto presetNow = audioProcessor.getLastPresetIndex();
    if (slotNow != lastShownSlot || dirtyNow != lastShownDirty
        || canUndoNow != lastShownCanUndo || canRedoNow != lastShownCanRedo)
    {
        lastShownSlot = slotNow;
        lastShownDirty = dirtyNow;
        lastShownCanUndo = canUndoNow;
        lastShownCanRedo = canRedoNow;
        updateWorkflowButtons();
    }
    if (presetNow != lastShownPreset)
    {
        lastShownPreset = presetNow;
        // Item IDs are index + 1, so 0 means "no selection" - what a user preset
        // (index -1) should show in the FACTORY combo.
        presetBox.setSelectedId (presetNow + 1, juce::dontSendNotification);
        refreshUserPresetList(); // a factory load clears the user-preset selection
    }
    // The box's SELECTED ENTRY follows the loaded preset, but a preset that is
    // loaded-and-then-edited keeps its entry while the machine drifts; that is
    // correct (the badge says EDITED) and needs no per-frame re-selection here.

    const auto userPresetNow = audioProcessor.getCurrentPresetName();
    const auto presetDirtyNow = audioProcessor.isPresetDirty();
    if (userPresetNow != lastShownUserPreset || presetDirtyNow != lastShownPresetDirty)
    {
        lastShownUserPreset = userPresetNow;
        lastShownPresetDirty = presetDirtyNow;
        updateWorkflowButtons();
    }

    // While the user turns knobs, the live state drifts away from the stored side, so
    // the active A/B slot is re-mirrored periodically and COPY A / COPY B always capture
    // the machine as it is right now. This used to run on EVERY timer frame: each call
    // deep-copies the whole parameter tree twice, so at 30 Hz that was sixty whole-tree
    // copies a second landing on the message thread in the middle of a knob drag - on top
    // of the repaint work. That is exactly when the GUI thread is busiest and the audio
    // thread has least headroom, and the resulting dropouts are heard as clicks. It now
    // runs at ~5 Hz and only while the editor is actually on screen, which is far faster
    // than a person can hear the EDITED badge appear.
    if (isShowing() && ++compareMirrorTick >= 6)
    {
        compareMirrorTick = 0;
        audioProcessor.updateActiveCompareSlot();
    }

    if (! isShowing() || physicsWorld == nullptr)
        return;

    physicsWorld->Step (1.0f / 30.0f, 6, 2);

    std::array<RenderOrb, decorativeOrbCount> nextFrame;
    for (std::size_t i = 0; i < decorativeOrbCount; ++i)
    {
        const auto position = physicsOrbs[i].body->GetPosition();
        nextFrame[i] = { juce::Point<float> (position.x, position.y),
                         physicsOrbs[i].radius,
                         physicsOrbs[i].colour };
    }

    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        renderOrbs = nextFrame;
    }

    // Finally, the 3D transport. It is fed last so every number it sees is the
    // one this tick settled on, and it is fed FIVE PLAIN FLOATS and nothing else:
    // the scene owns a second OpenGL context on its own render thread, and the
    // one thing that must never happen is a parameter tree or a JUCE Component
    // being read from there. Each is normalised here, in the thread that owns the
    // parameter, so the render thread has no idea what a dB is.
    if (const auto* rawDrive = audioProcessor.parameters.getRawParameterValue ("drive"))
        sceneDrive = rawDrive->load();

    tapeScene.setAudioState (
        // Output loudness over the range the meters themselves use for "moving":
        // below -48 dB the machine is at the noise floor and the scene goes quiet.
        juce::jlimit (0.0f, 1.0f, juce::jmap (telemetry.outputCombinedDb, -48.0f, 0.0f, 0.0f, 1.0f)),
        juce::jlimit (0.0f, 1.0f, sceneDrive),
        // Both compressors' total reduction, on the same six-decibel scale the
        // panel's glow uses, so the reels' rim light and the knobs agree.
        juce::jlimit (0.0f, 1.0f, std::abs (reduction) / 6.0f),
        // The transport's drift, re-centred: the panel carries it as 0..1 with
        // 0.5 as steady, and the scene wants it signed so the ribbon waves to
        // either side of the path rather than always to one.
        juce::jlimit (-1.0f, 1.0f, (driftAmount - 0.5f) * 2.0f),
        // The machine's own platter speed, already lagged in smoothedMachineSpeed
        // - so a STOP stops the reels and a held SPINDOWN coasts them down, and
        // the tape only transfers while the transport is actually running.
        juce::jlimit (0.0f, 1.0f, smoothedMachineSpeed),
        // The frame's own loudest sample, linear 0..1, drained from the processor
        // once per tick (getOutputPeakLevel is an exchange) so the scene's peak
        // lamp reads the same transient the meter needle just fell for.
        juce::jlimit (0.0f, 1.0f, audioProcessor.getOutputPeakLevel()));

    // The texture's doses follow the machine: the shimmer term scales with
    // drive and with the compressors' reduction, so the panel's surface is
    // alive where the signal pushes. One call feeds both renderers - the 2D
    // ink in paint() and the scene's uTextureWeights uniform.
    configureTextureWeights (sceneDrive,
                             juce::jlimit (0.0f, 1.0f, std::abs (reduction) / 6.0f));

    // The scene's own attach retries, on the same timer and the same bounded
    // budget as this editor's context: a child component cannot have a context
    // until the host has given it a native peer, and that may not have happened
    // on the first tick.
    tapeScene.serviceContextAttachment();
}

void FirstAudioProcessorEditor::resized()
{
    // The static texture layers (grain + wear) are sized to the window, so a
    // resize rebuilds them. Cheap enough at half resolution to pay on every
    // drag of the window's edge, and it keeps the texture from ever being
    // stretched over a size it was not built for.
    rebuildPanelTextureLayers();

    const auto layout = getEditorLayout();

    // Header captions: the small brand strip sits ABOVE the big title (they used to
    // share the same band and printed over each other), the subtitle starts right of
    // the title box, and all three stay clear of the theme button / status badge.
    brandLabel.setBounds (layout.header.getX() + 26, layout.header.getY() + 8, 120, 12);
    titleLabel.setBounds (layout.header.getX() + 25, layout.header.getY() + 24, 96, 46);
    subtitleLabel.setBounds (layout.header.getX() + 126, layout.header.getY() + 46, 220, 18);
    // The engine switches live in the header's right half, two per row - the
    // request was to move BYPASS / DELTA / POLARITY / AUTO GAIN up here, where
    // the theme button already sat. THEME and the status readout keep the
    // second row's right end; the switches take the row above them.
    //
    // Both rows are placed by a CURSOR that walks right-to-left and consumes
    // each item's own width, rather than by writing a coordinate per button.
    // It used to be five hard-coded x positions, and they did not add up: the
    // second row's AUTO GAIN ended at right-192 while THEME started at
    // right-202, so the two overlapped by ten pixels at every window size - the
    // kind of collision a table of literals cannot show you, because each
    // individual line is correct and only the SUM is wrong. With the cursor the
    // gap is the only number, and a row cannot overlap itself.
    //
    //  THE GRID. Five switches used to sit here in two rows of per-button
    //  widths (74 / 92 / 96 / 100 / 181), cursor-placed: no two buttons the
    //  same width, their columns never lining up, which is exactly the
    //  "arranged at random" look. The header is now ONE GRID - four equal
    //  columns, three rows - and every switch is one cell. Rows read left to
    //  right in the order a user scans them; nothing overlaps by construction
    //  (cells cannot intersect) and nothing drifts (no elastic gaps to grow).
    //
    //  Row three is the machine-state readout's own band. The first grid had
    //  two rows and put them into the drawn badge's plate (right - 205, +20,
    //  30 px tall) - switches ON the pill, which is what the user reported as
    //  "overlapping the Real-Time/Stereo caption AND its design". The badge
    //  lives on the grid's third row now: full grid width, 18 px, pill painted
    //  under it in paint(), lamp at its left end, nothing else ever placed
    //  there.
    //
    //  Clearance at the 780 px minimum: the grid is 386 px wide (4 x 92 + 3 x 6)
    //  and starts at header.right - 386 = x + 366 on a 780 panel, clearing the
    //  title block (ends x + 346) by 20 px at the tightest size and more above it.
    // ------------------------------------------------------------------------------
    const auto headerRight = layout.header.getRight();
    constexpr int headerGap = 6;
    constexpr int headerSwitchHeight = 22;
    constexpr int headerColumnWidth = 92;
    constexpr int headerColumns = 4;
    const auto headerRowOneY = layout.header.getY() + 10;
    const auto headerRowTwoY = layout.header.getY() + 36;
    const auto headerRowThreeY = layout.header.getY() + 62;
    constexpr int headerStatusHeight = 18;
    const auto headerGridLeft = headerRight
                              - (headerColumns * headerColumnWidth
                                 + (headerColumns - 1) * headerGap);

    const auto headerCell = [&] (juce::Component& component, int column, int rowY,
                                 int height = headerSwitchHeight)
    {
        component.setBounds (headerGridLeft + column * (headerColumnWidth + headerGap),
                             rowY, headerColumnWidth, height);
    };

    // Row one: the four engine switches, equal cells, one grid.
    headerCell (deltaButton,     0, headerRowOneY);
    headerCell (bypassButton,    1, headerRowOneY);
    headerCell (polarityButton,  2, headerRowOneY);
    headerCell (autoGainButton,  3, headerRowOneY);

    // Row two: THEME, then the language pair - caption cell + a box that spans
    // the last two cells.
    headerCell (themeButton,     0, headerRowTwoY);
    headerCell (languageLabel,   1, headerRowTwoY);
    languageLabel.setJustificationType (juce::Justification::centredRight);
    languageBox.setBounds (headerGridLeft + 2 * (headerColumnWidth + headerGap),
                           headerRowTwoY,
                           2 * headerColumnWidth + headerGap,
                           headerSwitchHeight);

    // Row three is the machine-state readout's OWN band, full grid width minus
    // the header's corner screw. The
    // first grid put row-two switches into the drawn badge's 30 px-tall plate
    // (right - 205, y + 20) - switches ON the pill, the user's complaint. A
    // two-row grid has no cell a 30 px badge fits in, so the header grew a
    // third row: the pill returns to the header as a strip under the
    // switches, painted in paint() exactly on these bounds, lamp at its left
    // end, text after it. BYPASS ON swaps the text to BYPASSED / DRY from the
    // telemetry timer; nothing else is ever placed on this row, so the pill
    // cannot be collided with by construction.
    statusLabel.setBounds (headerGridLeft, headerRowThreeY,
                           headerColumns * headerColumnWidth
                               + (headerColumns - 1) * headerGap - 18,
                           headerStatusHeight);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (statusLabel.getFont().withHeight (9.0f));
    statusLabel.setInset (juce::BorderSize<int> (0, 26, 0, 0));

    // ------------------------------------------------------------------
    //  The deck.
    //
    //  Six bands, none of them shared with another:
    //   y +   6  the DECK heading and the build id
    //   y +  30  MODEL     tape type | speed | BPM
    //   y +  74  SWITCHES  oversampling | instrument | GL | HARMONICS | SUB
    //              FUND | ANTI PHASE
    //   y + 116  the six type switches, their captions above their boxes
    //   y + 183  TRANSPORT keys, the machine state, the mode switch
    //   y + 217  the preset workflow chain
    //   y + 253  the preset badge band          (the deck is 278 px)
    //
    //  BYPASS / DELTA / POLARITY / AUTO GAIN are not here: they moved to the
    //  header (see above), and the delay trio is a tab member of SPACE.
    //
    //  The three machine readouts were a vertical stack pinned to the deck's
    //  right edge - 130 px wide and 88 px tall. That is why the type row could
    //  not start until y + 168, why the deck had to be 322 px to hold it, and
    //  why the row it shared with OVER, INSTRUMENT and GL had two hundred
    //  pixels of nothing between them at the minimum width. They are three
    //  caption-over-value pairs side by side now, and each fits inside the
    //  32 px band the switches share, which is what brings the deck back to
    //  278 and takes forty-four pixels off the editor's minimum height.
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    //  The deck's vertical rhythm.
    //
    //  Six bands at hardcoded offsets inside a rectangle of exactly 278 px is
    //  why the deck could not be made shorter: the deck is now elastic (see
    //  getEditorLayout), and these offsets are what has to follow it. They are
    //  written as a TABLE of bands - each one's own height - and only the GAPS
    //  between them compress. A band's height is never touched, so a control
    //  cannot be squeezed by a shorter window; a gap is what gives way, and it
    //  is floored, so two bands can never end up touching however short the
    //  deck gets.
    //
    //  At the reference height the scale is exactly 1 and every offset below is
    //  the number that is written in the table - so this is a refactor with no
    //  visual effect at the size the window opens at, which is the only way to
    //  be sure of a change made without being able to look at it.
    // ------------------------------------------------------------------
    struct DeckBand
    {
        int top;
        int height;
    };

    constexpr DeckBand referenceBands[] = {
        {   6, 16 },   // the heading strip: "Deck" and the build id
        {  30, 32 },   // MODEL
        {  74, 32 },   // SWITCHES
        { 116, 45 },   // the six type selectors, caption over box
        { 170, 45 },   // TRANSPORT: caption band, then the keys
        { 217, 32 },   // the preset chain
        { 253, 14 }    // the badge line
    };

    constexpr int deckReferenceHeight = 278;
    constexpr int minimumBandGap = 2;
    constexpr auto bandCount = static_cast<int> (juce::numElementsInArray (referenceBands));

    // The working copy is what the compression below rewrites; the table above
    // stays the written-down reference for the size the panel was drawn at.
    DeckBand deckBands[bandCount];

    for (int i = 0; i < bandCount; ++i)
        deckBands[i] = referenceBands[i];

    {
        // The gaps between the bands as the table states them, and then the gaps
        // they are allowed once the deck is as short as it can get.
        int gap[bandCount - 1] {};
        int gapTotal = 0;

        for (int i = 1; i < bandCount; ++i)
        {
            gap[i - 1] = referenceBands[i].top
                       - (referenceBands[i - 1].top + referenceBands[i - 1].height);
            gapTotal += gap[i - 1];
        }

        const auto shrink = juce::jmax (0, deckReferenceHeight - layout.deck.getHeight());
        const auto wantedGapTotal = juce::jmax ((bandCount - 1) * minimumBandGap, gapTotal - shrink);
        const auto gapScale = gapTotal > 0 ? static_cast<float> (wantedGapTotal)
                                           / static_cast<float> (gapTotal)
                                           : 0.0f;

        for (int i = 1; i < bandCount; ++i)
            deckBands[i].top = deckBands[i - 1].top + deckBands[i - 1].height
                             + juce::jmax (minimumBandGap,
                                           juce::roundToInt (gap[i - 1] * gapScale));
    }

    // [&] and not [&layout]: the band table is a local array, and a lambda that
    // names one thing in its capture list captures nothing else - so a narrow
    // capture silently leaves deckBands uncaptured.
    const auto deckTop = [&] (int band) { return layout.deck.getY() + deckBands[band].top; };

    deckHeadingLabel.setBounds (layout.deck.getX() + 18, deckTop (0), 150, 16);

    // The build id rides the deck's heading strip, right-aligned and stopping short of
    // the reel (whose left edge is deck.right - 56). This strip is the only full-width
    // band near the top of the panel that is guaranteed empty at the 780 px minimum:
    // the header's equivalent gap shrinks to about 58 px there, and the switches row
    // is reserved for the drifting particles.
    buildLabel.setBounds (layout.deck.getX() + 190, deckTop (0),
                          layout.deck.getWidth() - 280, 16);

    // ------------------------------------------------------------------
    //  Spreading a row across the deck.
    //
    //  The deck was drawn for the 752 px minimum and the editor opens at 1060,
    //  so every row that pinned a group to the left and another to the right
    //  left a hole in the middle that GREW with the window: 32 to 96 px of it
    //  at the minimum, and 312 to 486 px at the size the window actually opens
    //  at. Nothing was wrong with any single number - the widths were right and
    //  the anchors were right - and the sum of the two was a deck with its
    //  middle missing. A layout written for one width does not look wrong at
    //  that width; it looks wrong at every other one, which is the whole of
    //  what "the deck is crooked" meant.
    //
    //  So the slack is shared. Half of it opens up the gaps and half goes into
    //  the items themselves, which is what stops a row from looking like small
    //  controls drifting apart: a row reads as one thing that grew, rather than
    //  as three things with holes between them.
    //
    //  Three properties keep this safe rather than merely different:
    //
    //    - every item's base width is still written down, next to its own name,
    //      so a row can never claim more space than its content needs, and the
    //      same number describes the item at every panel size;
    //    - the gap is FLOORED at the constant the row used before, so at the
    //      minimum width this is a small perturbation of the layout that
    //      shipped, and the guarantee is one-sided - it can only ever add space,
    //      never take any away, and so can never bring two controls closer
    //      together than they have ever been;
    //    - a caption and the control it names are GLUED: their gap is written
    //      in the table and is never elastic, and a caption stacked on its own
    //      readout rides the readout's column rather than adding a second one.
    //      That is the exact failure the previous pass fixed on this panel -
    //      INSTRUMENT's caption printed straight through its own combo box -
    //      and a rule that redistributed slack between the two would be a way
    //      of walking back into it.
    //
    //  The row returns the widest gap it left, so the drifting particles are
    //  told where the free space actually is instead of being told a pair of
    //  coordinates that was only ever right at one width.
    // ------------------------------------------------------------------
    struct DeckRowItem
    {
        juce::Component* component;
        int width;         // the column this item opens; ignored when sameColumn
        int gapAfter;      // -1 elastic, or a fixed gap; ignored when sameColumn
        int y;             // relative to the row's own y
        int height;
        bool sameColumn;   // rides the column the previous item opened (a
                          // caption above its own readout, or its own box)
    };

    const auto placeDeckRow = [] (const std::vector<DeckRowItem>& items,
                                  int left, int right, int rowY, int minimumGap)
    {
        int content = 0;
        int elasticGaps = 0;

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (! items[i].sameColumn)
                content += items[i].width;

            // A gap belongs to the item that ENDS a column: the caption of a
            // horizontal pair, and the value of a stacked one.
            if (i + 1 < items.size() && ! items[i + 1].sameColumn)
            {
                if (items[i].gapAfter < 0)
                {
                    content += minimumGap;
                    ++elasticGaps;
                }
                else
                {
                    content += items[i].gapAfter;
                }
            }
        }

        // Never negative: a row whose own widths are wider than the panel packs
        // left at the minimum gap rather than overlapping itself.
        const auto slack = juce::jmax (0, right - left - content);

        // The gap grows, but not without limit. Past a point, a wider gap stops
        // reading as "this row was justified" and starts reading as "these two
        // controls are unrelated", and on the model row that point arrives
        // before the maximum window does: at 1500 px the unbounded share put
        // two hundred and thirty pixels between TAPE TYPE and SPEED. So the
        // gap's share is capped and everything above the cap goes into the
        // items instead - a combo box that is a little wider looks like a combo
        // box, and a gap that is a little wider looks like a mistake.
        //
        // 48 px is chosen against the numbers rather than by taste. It sits
        // just above what the model row asks for at the 780 px minimum (a 37 px
        // share on top of its 16 px base, i.e. 53 px), so the tightest window
        // this plugin can open at is laid out exactly as it was before the cap
        // existed - a minimum that changed shape would be a bug, not a layout.
        // Above the minimum it takes effect: the model row's gap is 64 px at
        // 900, at 1060 and at 1500 instead of 83, 123 and 233.
        constexpr int maximumGapGrowth = 48;
        const auto wantedGapShare = elasticGaps > 0
                                      ? static_cast<int> (slack * 0.5) / elasticGaps : 0;
        const auto gapShare = juce::jmin (wantedGapShare, maximumGapGrowth);
        const auto usedByGaps = gapShare * elasticGaps;

        // The width share divides by COLUMNS, not by items: a stacked caption
        // rides its column and must not eat a share of its own. Dividing by the
        // item count gave five-share voids on every row that stacks caption over
        // value - slack the row collected and never spent, which the user saw as
        // controls spaced 'at random' with a hole at the row's end.
        int columnCount = 0;
        for (const auto& item : items)
            if (! item.sameColumn)
                ++columnCount;

        const auto widthShare = columnCount == 0
                                  ? 0
                                  : (slack - usedByGaps) / columnCount;

        auto widest = juce::Range<int> (0, 0);
        int edge = left;
        int columnLeft = left;
        int columnWidth = 0;

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];

            // A stacked item does not open a column: it rides the one the item
            // before it opened, at that item's width, so a caption and the
            // readout under it are always the same width by construction and
            // there is no second number that could disagree with the first.
            if (! item.sameColumn)
            {
                columnLeft = edge;
                columnWidth = item.width + widthShare;
            }

            item.component->setBounds (columnLeft, rowY + item.y, columnWidth, item.height);

            // The gap belongs to the item that ENDS the column, so a stacked
            // pair still leaves the row with the gap it was promised.
            if (i + 1 < items.size() && ! items[i + 1].sameColumn)
            {
                const auto elastic = item.gapAfter < 0;
                const auto gap = elastic ? minimumGap + gapShare : item.gapAfter;

                if (elastic && gap > widest.getLength())
                    widest = { columnLeft + columnWidth, columnLeft + columnWidth + gap };

                edge = columnLeft + columnWidth + gap;
            }
        }

        return widest;
    };

    constexpr int deckRightInset = 14;

    // The reel and the tape ribbon own the deck's top-right corner, and the ribbon
    // hangs down through the model row's band: it starts at the reel (deck.right -
    // 42, y + 26) and falls to y + 60, which is the row the tape box and the BPM
    // readout sit in. So the model row stops 70 px short - twenty-eight to the
    // right of the ribbon's leftmost point - and every other row uses the standard
    // inset. This is not a margin that has to be re-tuned per row; it is the
    // ribbon's own width, and the rows below it do not need it because the ribbon
    // ends above them.
    constexpr int reelCorridor = 70;

    // ---- row 1: TRANSPORT SPEED ---------------------------------------------
    //  Everything that reads as a RATE lives in one row now, which is what the
    //  user asked for: the tape speed (ips) and the turntable's motor speed
    //  (RPM) sit side by side, and the host-tempo readout rides with them -
    //  three rate readouts a user scans as one group instead of MODEL | SPEED
    //  | BPM, where the model sat between two kinds of speed.
    //  The empty columns to the right are deliberate: the type row below has
    //  six columns and the rows should breathe together, not stretch to fill.
    {
        const std::vector<DeckRowItem> row {
            { &speedLabel,       108, -1,  0, 15, false },
            { &speedBox,           0, -1, 15, 30, true  },
            { &vinylSpeedLabel,  108, -1,  0, 15, false },
            { &vinylSpeedBox,      0, -1, 15, 30, true  },
            { &bpmLabel,         108, -1,  0, 15, false },
            { &bpmReadout,         0, -1, 15, 13, true  }
        };

        placeDeckRow (row, layout.deck.getX() + 18, layout.deck.getRight() - reelCorridor,
                      deckTop (1), 14);
    }

    // ---- row 2: SWITCHES ----------------------------------------------------
    // OVERSAMPLING and GL are NOT in this row: they are SETTINGS-tab members and
    // live in the member row under that tab's knobs (placeDeckSwitch below).
    // They used to sit here on EVERY tab while being visible only on SETTINGS -
    // so five tabs out of six showed two unexplained holes in this line.
    //
    //  Four equal 108 px columns, the type row's grid again: INSTRUMENT, then
    //  the three readout pairs - caption above value, one column each, all
    //  standing exactly above the type row's columns.
    {
        const std::vector<DeckRowItem> row {
            { &instrumentLabel,  108, -1,  0, 15, false },
            { &instrumentBox,      0, -1, 15, 30, true  },

            { &harmonicsLabel,   108, -1,  0, 15, false },
            { &harmonicsReadout,   0, -1, 15, 13, true  },
            { &subfundLabel,     108, -1,  0, 15, false },
            { &subfundReadout,     0, -1, 15, 13, true  },
            { &antiPhaseLabel,   108, -1,  0, 15, false },
            { &antiPhaseReadout,   0, -1, 15, 13, true  }
        };

        const auto freeGap = placeDeckRow (row, layout.deck.getX() + 18,
                                           layout.deck.getRight() - deckRightInset,
                                           deckTop (2), 12);

        // The readouts are the only deck text whose width now depends on the
        // panel, so they are the only ones that can run out of room - and they
        // are also the only ones whose TEXT changes while the editor is open
        // ("idle - no note tracked", "412.5 Hz 87 %"). setMinimumHorizontalScale
        // is the one fit that keeps working: it is a property of the label, so
        // every later setText is measured against the width the row gave it,
        // rather than being clipped at whatever size it happened to be at when
        // the last resize happened to run.
        for (auto* label : { &harmonicsLabel, &harmonicsReadout,
                             &subfundLabel, &subfundReadout,
                             &antiPhaseLabel, &antiPhaseReadout })
            label->setMinimumHorizontalScale (0.55f);

        // Where the particles may drift: the gap this row actually left. Under
        // twenty pixels there is nowhere free to put them, and drawing them
        // anyway would put decoration on top of a control - the one thing this
        // whole pass exists to stop. paint() skips them when this is empty.
        deckParticleCorridorX = freeGap.getLength() >= 20 ? freeGap
                                                          : juce::Range<int> (0, 0);
        deckParticleCorridorY = juce::Range<int> (deckTop (2) + 2, deckTop (2) + 30);
    }

    // ---- row 3: the six type selectors --------------------------------------
    {
        // The model selector (renamed TAPE, its old caption read MODEL) joined
        // the row of type selectors - the user read MODEL | SPEED | BPM as
        // three unrelated things on one line, when the selector is a TYPE like
        // VINYL or VALVE and belongs with them. Six columns as before, so the
        // row arithmetic and the 108 px column are unchanged.
        //
        // 108 per column, not 100: the longest stock values ("Fender Blackface",
        // "Jensen JT-11P") ellipsised in the deck's own combo drawing at the old
        // width on every panel under ~1400 px. 108 is the largest the row holds at
        // the 780 px minimum ((720 - 5 x 14 gaps) / 6 = 108), and above that width
        // the row's elastic gap shrinks before any column does.
        const std::vector<DeckRowItem> row {
            { &tapeTypeLabel,        108, -1,  0, 15, false },
            { &tapeTypeBox,            0, -1, 15, 30, true  },
            { &vinylTypeLabel,       108, -1,  0, 15, false },
            { &vinylTypeBox,           0, -1, 15, 30, true  },
            { &valveTypeLabel,       108, -1,  0, 15, false },
            { &valveTypeBox,           0, -1, 15, 30, true  },
            { &ampTypeLabel,         108, -1,  0, 15, false },
            { &ampTypeBox,             0, -1, 15, 30, true  },
            { &transformerTypeLabel, 108, -1,  0, 15, false },
            { &transformerTypeBox,     0, -1, 15, 30, true  },
            { &digitalTypeLabel,     108, -1,  0, 15, false },
            { &digitalTypeBox,         0, -1, 15, 30, true  }
        };

        placeDeckRow (row, layout.deck.getX() + 18, layout.deck.getRight() - deckRightInset,
                      deckTop (3), 14);
    }

    // ---- row 4: TRANSPORT ---------------------------------------------------
    {
        const std::vector<DeckRowItem> row {
            { &transportStopButton,   62,  4,  0, 28, false },
            { &transportPlayButton,   62,  4,  0, 28, false },
            { &transportStartButton,  62,  4,  0, 28, false },
            { &spindownButton,        74, -1,  0, 28, false },
            { &deckHintLabel,        190, -1,  2, 24, false },
            { &modeCycleButton,      150, -1,  0, 32, false }
        };

        const auto transportRowY = deckTop (4) + 13;

        placeDeckRow (row, layout.deck.getX() + 18, layout.deck.getRight() - deckRightInset,
                      transportRowY, 14);

        // The two transport captions sit ABOVE the key bank rather than beside
        // their own key, so they cannot be a column of the row - they are placed
        // from the buttons the row just gave them. Reading the placed bounds
        // back is what keeps them correct at every width: written as
        // `keyWidth * 3 + gap * 2` they would name a span three keys wider than
        // the bank actually is once the row starts growing them.
        transportLabel.setBounds (transportStopButton.getX(), transportRowY - 13,
                                  transportStartButton.getRight() - transportStopButton.getX(), 12);
        spindownLabel.setBounds (spindownButton.getX(), transportRowY - 13,
                                 spindownButton.getWidth(), 12);

        transportLabel.setFont (shrinkingFont (transportLabel.getText(), 7.0f,
                                              juce::Font::plain,
                                              static_cast<float> (transportLabel.getWidth()) - 3.0f));
        spindownLabel.setFont (shrinkingFont (spindownLabel.getText(), 7.0f,
                                              juce::Font::plain,
                                              static_cast<float> (spindownLabel.getWidth()) - 3.0f));
    }

    // The preset workflow chain is eleven items and it did not fit: written as
    // a left-to-right chain of `previous.getRight() + gap`, the last item - the
    // compare badge - ended sixteen pixels past the deck's right edge at the
    // 780 px minimum, so it was drawn over the panel border and the frame.
    //
    // It is walked from BOTH ends: the head of the chain from the deck's left
    // inset, the tail from its right inset, each with the same cursor
    // discipline as the header's switch rows. Every item's width is written
    // once, next to its own name, and the two chains meet in the middle - so
    // adding a button to either end costs one line and cannot push the other
    // end off the panel, which is the failure a single chain always has. It is
    // not spread like the rows above because it already spans the full width at
    // the minimum: there is no slack to share, and a gap that grew with the
    // window would only pull its two halves further apart.
    const auto presetRowY = deckTop (5);
    const auto presetRowHeight = 32;
    constexpr int presetRowGap = 5;

    const auto placeForward = [presetRowY, presetRowHeight, presetRowGap]
                                  (int& edge, juce::Component& component, int width)
    {
        component.setBounds (edge, presetRowY, width, presetRowHeight);
        edge += width + presetRowGap;
    };

    const auto placeBackward = [presetRowY, presetRowHeight, presetRowGap]
                                   (int& edge, juce::Component& component, int width)
    {
        component.setBounds (edge - width, presetRowY, width, presetRowHeight);
        edge -= width + presetRowGap;
    };

    presetHeadingLabel.setBounds (layout.deck.getX() + 18, deckTop (5) + 9, 46, 16);

    {
        // Head, from the left: the factory list, the user list, save, delete.
        // 120 and 94 rather than 128 and 100: at the 780 px minimum the two
        // chains meet with two pixels between DELETE and COPY A, which is a
        // clearance, not a margin. Fourteen pixels taken off the two widest
        // items - both of them lists that shorten their own font before they
        // clip - buys sixteen, and the chain then has room for a button added
        // at either end without the two ends touching.
        //
        //  THE WIDE-WINDOW HOLE. Both chains were laid out against the 780 px
        //  minimum, and on anything wider the unspent width sat between them
        //  as one hole that grew with the panel - the deck's last "random
        //  gap". The hole belongs to the two LISTS: they are the chain's
        //  widest members and the only two items a wider bounds actually
        //  improves (a longer preset name shown instead of a longer emptiness;
        //  a button or a badge would only read as stretched). The lists absorb
        //  it in shares - 60/40, each capped - and at the minimum the shares
        //  are zero by construction, so the tight layout is untouched.
        constexpr int presetBoxWidth  = 120;
        constexpr int userPresetWidth =  94;
        constexpr int headWidths = presetBoxWidth + userPresetWidth + 40 + 38;
        constexpr int tailWidths = 54 + 44 + 44 + 54 + 64 + 64;
        const auto chainSpan = (layout.deck.getRight() - 14)
                                   - (layout.deck.getX() + 66);
        const auto chainVoid = juce::jmax (0, chainSpan - (headWidths + tailWidths
                                                              + 9 * presetRowGap));
        const auto presetGrowth = juce::jmin (chainVoid * 3 / 5, 160);
        const auto userGrowth   = juce::jmin (chainVoid - presetGrowth, 120);

        int headEdge = layout.deck.getX() + 66;
        placeForward (headEdge, presetBox,          presetBoxWidth  + presetGrowth);
        placeForward (headEdge, userPresetBox,      userPresetWidth + userGrowth);
        placeForward (headEdge, savePresetButton,  40);
        placeForward (headEdge, deletePresetButton, 38);
    }

    {
        // Tail, from the right: the badge, REDO, UNDO, COMPARE, A, B.
        int tailEdge = layout.deck.getRight() - 14;
        placeBackward (tailEdge, compareBadgeLabel, 54);
        placeBackward (tailEdge, redoButton,        44);
        placeBackward (tailEdge, undoButton,        44);
        placeBackward (tailEdge, compareButton,     54);
        placeBackward (tailEdge, copyBButton,       64);
        placeBackward (tailEdge, copyAButton,       64);
    }

    // The badge band is inset further than the other deck text (22 px instead of 18)
    // and its caption is fitted to the width it actually has, so neither "FACTORY
    // STATE" nor a long user-preset name can run into the panel edges or be clipped.
    //
    // The machine state moved here from the header: it is a readout of what the
    // engine is doing, and this is the band the deck's other readouts (the
    // preset badge, the compare badge) already live on. It takes a fixed cell
    // off the band's right end and the preset badge gives up exactly that
    // width, so a long preset name shrinks into what is left rather than
    // printing under the state text - the shrinking font below measures the
    // label's ACTUAL bounds, so narrowing it is the whole fix.
    // The state readout is back in the header on its drawn pill (see the header
    // grid above), so the badge line is the preset badge's alone again - full
    // band width, the pill painted UNDER this line in paint() needs no cell.
    presetBadgeLabel.setBounds (layout.deck.getX() + 22, deckTop (6),
                                layout.deck.getWidth() - 44, 14);
    presetBadgeLabel.setFont (shrinkingFont (presetBadgeLabel.getText(), 8.0f,
                                             juce::Font::plain,
                                             static_cast<float> (presetBadgeLabel.getWidth()) - 4.0f));


    controlsHeadingLabel.setBounds (layout.controls.getX() + 18, layout.controls.getY() + 10, 210, 19);
    const auto controlsHintRight = layout.controls.getRight() - 12;
    const auto controlsHintLeft = juce::jmax (layout.controls.getX() + 240,
                                              controlsHeadingLabel.getRight() + 24);
    controlsHintLabel.setBounds (controlsHintLeft, layout.controls.getY() + 10,
                                 juce::jmax (0, controlsHintRight - controlsHintLeft), 19);

    // The 3D transport's window: the deck's top-right block, which is the one
    // region resized() keeps free of controls. reelCorridor is 70 px wide and the
    // heading band starts six px down, so the scene gets a 64 x 58 block that is
    // the same size at every panel size - which is what lets the scene be laid out
    // in its own units and framed by the camera rather than by a pixel count.
    // It stops 8 px short of the switches row below it, so the two never touch.
    tapeScene.setBounds (layout.deck.getRight() - reelCorridor,
                         layout.deck.getY() + 6,
                         reelCorridor - 6,
                         58);

    // ------------------------------------------------------------------
    //  The tab bar.
    //
    //  The three groups used to be one five-row block of all twenty-one knobs, with a
    //  small caption floating in the row gap above each group. Those were the panel's
    //  worst-placed furniture: the gap above row 3 lies INSIDE row 2 and the gap above
    //  row 4 lies inside row 3, so SATURATION CORE was painted on top of the row of
    //  knobs above it and HEAD / TRANSPORT on top of the next one, and at 13 px tall
    //  the text ran into the knob captions behind it. Nothing about that placement
    //  could have been right - there is no row of its own for a caption to sit in when
    //  the rows are full.
    //
    //  The captions are gone and the groups are real tabs, so each is named on a
    //  button tall enough to read, the knobs underneath get a whole grid to
    //  themselves, and only the selected tab's controls are on screen at all.
    // ------------------------------------------------------------------
    const auto tabBarLeft = layout.controls.getX() + 14;
    const auto tabBarTop = layout.controls.getY() + 34;
    const auto tabBarHeight = 24;
    // The gap shrinks as tabs are added so the buttons stay as wide as they can:
    // ten pages at a fixed 6 px each spent 54 of the bar's ~700 px on nothing, and
    // the caption is auto-sized to the button, so the width is what decides whether
    // a name is legible. Six is kept while it fits and reduced past that.
    const auto tabGap = numTabs <= 7 ? 6 : (numTabs <= 9 ? 4 : 3);
    const auto tabButtonWidth = (layout.controls.getWidth() - 28
                                 - tabGap * (numTabs - 1)) / numTabs;

    for (int tab = 0; tab < numTabs; ++tab)
        tabButtons[static_cast<std::size_t> (tab)]
            .setBounds (tabBarLeft + tab * (tabButtonWidth + tabGap),
                        tabBarTop, tabButtonWidth, tabBarHeight);
    metersHeadingLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 8, 130, 18);
    metersHintLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 27, 150, 14);
    const auto compressorLabelWidth = 120;
    compressorLabel.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                               layout.meters.getY() + 8, compressorLabelWidth, 18);
    compressorReadout.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                                 layout.meters.getY() + 27, compressorLabelWidth, 14);

    // The grid starts below the divider paint() drew, with a little air under it. The
    // 14 px undoes the reduced() inset, so the offset is measured from the panel edge
    // that controlsDividerOffset is measured from, not from the inset grid.
    auto grid = layout.controls.reduced (14);
    grid.removeFromTop (controlsDividerOffset - 14 + 8);
    grid.removeFromBottom (8);

    // Only the ACTIVE tab is laid out, and its own control count decides the row
    // count, so a tab is never sized as if it still had to hold the whole panel's
    // knobs. Tabs hold between three and nine controls; NOISE, the fullest, comes
    // to three rows of tabColumns, the rest to two.
    const auto& activeTab = tabSpecs[static_cast<std::size_t> (currentTab)];
    const auto tabControlCount = static_cast<int> (activeTab.count);
    const auto cellWidth = grid.getWidth() / tabColumns;

    // Which tab is live, looked up by name from the tabSpecs table - the single
    // source of truth - rather than carried as numbers that drift when a page is
    // added or reordered. Hoisted ABOVE the row arithmetic because the member row
    // is decided by the same flags the layout chain below reads.
    const auto settingsTab = index_of_tab_named ("SETTINGS") == currentTab;
    const auto spaceTab = index_of_tab_named ("SPACE") == currentTab;
    const auto characterTab = index_of_tab_named ("CHARACTER") == currentTab;
    const auto driveTab = index_of_tab_named ("DRIVE") == currentTab;
    const auto inEqTab = index_of_tab_named ("IN EQ") == currentTab;
    const auto outEqTab = index_of_tab_named ("OUT EQ") == currentTab;
    const auto machineTab = index_of_tab_named ("MACHINE") == currentTab;
    const auto dynamicsTab = index_of_tab_named ("DYN") == currentTab;

    // The knob rows the active tab needs (zero on SETTINGS, which holds no knobs),
    // and one row more when the tab owns member switches - the non-knob combos and
    // pills placeDeckSwitch lays out under the knobs. SETTINGS is the worst case
    // and the reason the arithmetic is shared: with no knobs at all, sizing the
    // grid for knob rows alone left its whole height for a member row that was
    // then appended BELOW the grid, off the panel - GL and OVERSAMPLING were
    // clipped by the window's bottom edge on the very tab that holds nothing else.
    const auto knobRows = (tabControlCount + tabColumns - 1) / tabColumns;
    const auto memberRows = (settingsTab || spaceTab || characterTab
                             || dynamicsTab || inEqTab || outEqTab
                             || machineTab || driveTab) ? 1 : 0;

    // The member band is a FIXED strip - a caption band plus a control band -
    // reserved under the knob rows, and the knob rows share what is left. A
    // uniform division would tax a page's knobs for a single combo: DRIVE holds
    // twelve knobs in three rows, and splitting the grid four ways instead of
    // three would cost every one of them a quarter of its height. A fixed strip
    // takes a fixed price, and every knob page keeps the row height its own knob
    // count asks for. 50 px is the label band (17) plus the control's 30 at its
    // 20 px offset - the geometry placeDeckSwitch below already assumes.
    constexpr int memberBandHeight = 50;
    const auto knobAreaHeight = grid.getHeight()
                              - (memberRows != 0 ? memberBandHeight : 0);
    const auto knobRowHeight = knobRows > 0 ? knobAreaHeight / knobRows : 0;

    for (int slot = 0; slot < tabControlCount; ++slot)
    {
        const auto i = activeTab.controls[slot];
        const auto row = slot / tabColumns;
        const auto column = slot % tabColumns;
        auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                          grid.getY() + row * knobRowHeight,
                                          column == tabColumns - 1
                                              ? grid.getRight() - (grid.getX() + column * cellWidth)
                                              : cellWidth,
                                          row == knobRows - 1 && memberRows == 0
                                              ? grid.getBottom() - (grid.getY() + row * knobRowHeight)
                                              : knobRowHeight);
        controlLabels[i].setBounds (cell.getX() + 5, cell.getY() + 1,
                                    cell.getWidth() - 10, 17);
        auto sliderBounds = cell.reduced (5);
        sliderBounds.removeFromTop (18);
        controls[i].setBounds (sliderBounds);
    }

    // The deck's non-knob switches occupy grid cells of their own tab, exactly
    // where a knob would sit - same cell arithmetic, same label-above-control
    // convention - so they are laid out HERE rather than on the fixed deck rows.
    // A combo is taller than a knob cell wants, so the box rides the cell's lower
    // half under its label.
    //
    // Tab members (the combos and switches that are not knobs) ride the SAME
    // grid the knobs use, in one reserved band under the knob rows. This is the
    // systemic fix for the crooked DELAY TYPE / SYNC / RATE trio: the old code
    // hand-computed each member's rectangle from raw coordinates, so every
    // caller reinvented the cell arithmetic and the results could (and did)
    // collide with the knob rows and with each other. Now the arithmetic exists
    // exactly once: label band 17 px, control band 30 px at its 20 px offset,
    // the control capped at 30 px so it can never overflow the band.
    // memberRowY is computed from the same arithmetic: the band starts where
    // the knob rows end (at the grid's top on SETTINGS, which holds no knobs)
    // and runs to the grid's bottom, so the division's remainder lands inside
    // the band rather than above it.
    const auto memberRowY = grid.getY() + knobRows * knobRowHeight;
    const auto placeDeckSwitch = [&] (juce::Label& label, juce::Component& box,
                                      int column, int columnsWide,
                                      const juce::String& caption)
    {
        auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                          memberRowY,
                                          columnsWide == tabColumns
                                              ? grid.getRight() - (grid.getX() + column * cellWidth)
                                              : columnsWide * cellWidth
                                                  + (columnsWide - 1) * (cellWidth / columnsWide),
                                          grid.getBottom() - memberRowY);
        // This setText() every layout is also why the GL label's constructor
        // emptiness is safe: the caption is re-written here every time its tab is
        // live, so a resize between tab switches can never leave a stale one.
        label.setText (caption, juce::dontSendNotification);
        label.setBounds (cell.getX() + 5, cell.getY() + 1, cell.getWidth() - 10, 17);
        box.setBounds (cell.getX() + 12, cell.getY() + 20,
                       cell.getWidth() - 24, juce::jmin (30, cell.getHeight() - 22));
    };

    // Visibility for the deck switches is re-asserted here rather than only in
    // setCurrentTab, because resized() also runs from setSize() in the constructor
    // - BEFORE some of these widgets had a parent - and a show/hide decision made
    // only at tab-switch time left the switches permanently visible on tabs that
    // were not theirs. The active tab decides; every resize re-applies it. The
    // flags themselves are read from the tabSpecs table at the top of the grid
    // section above, where the member-row arithmetic needs them too.

    // The four new lists follow the same rule as the deck switches: each is a
    // full member of exactly one tab, so it is visible only while that tab is.
    diPadLabel.setVisible (driveTab);
    diPadBox.setVisible (driveTab);
    tracksLabel.setVisible (machineTab);
    tracksBox.setVisible (machineTab);
    inputEqOrderLabel.setVisible (inEqTab);
    inputEqOrderBox.setVisible (inEqTab);
    outputEqOrderLabel.setVisible (outEqTab);
    outputEqOrderBox.setVisible (outEqTab);
    // The corner switches follow their own equaliser's page, like the ORDER
    // combo beside them.
    inputEqHpButton.setVisible (inEqTab);
    inputEqLpButton.setVisible (inEqTab);
    outputEqHpButton.setVisible (outEqTab);
    outputEqLpButton.setVisible (outEqTab);
    oversamplingLabel.setVisible (settingsTab);
    oversamplingBox.setVisible (settingsTab);
    glButton.setVisible (settingsTab);
    glLabel.setVisible (settingsTab);
    // The UI-sounds switch is on the same page as GL and OVERSAMPLING - it is the
    // same kind of engine-level preference - so it follows the same tab.
    uiSoundsButton.setVisible (settingsTab);
    delayTypeLabel.setVisible (spaceTab);
    delayTypeBox.setVisible (spaceTab);
    delayRateLabel.setVisible (spaceTab);
    delayRateBox.setVisible (spaceTab);
    delaySyncLabel.setVisible (spaceTab);
    delaySyncButton.setVisible (spaceTab);

    // The neural model picker belongs to the DYNAMICS page, beside the NEURAL
    // knob it feeds, so it follows the same tab as that knob.
    loadNeuralButton.setVisible (dynamicsTab);
    clearNeuralButton.setVisible (dynamicsTab);
    neuralStatusLabel.setVisible (dynamicsTab);

    // The three vinyl selectors moved to the CHARACTER page: they re-voice the
    // whole vinyl stage, so they live with the machine's other voicing rather
    // than beside the record's faults (which are on NOISE now).
    vinylGenerationLabel.setVisible (characterTab);
    vinylGenerationBox.setVisible (characterTab);
    vinylTurntableLabel.setVisible (characterTab);
    vinylTurntableBox.setVisible (characterTab);
    vinylCartridgeLabel.setVisible (characterTab);
    vinylCartridgeBox.setVisible (characterTab);

    if (machineTab)
    {
        // MACHINE: seven knobs fill two rows (the second holds three), so the
        // member row under them is where the machine's TRACKS selector lives.
        placeDeckSwitch (tracksLabel, tracksBox, 0, 1, "TRACKS");
    }
    else if (driveTab)
    {
        // DRIVE: twelve knobs in three full rows, so the member row under them
        // carries the DI pad - the front end's input switch.
        placeDeckSwitch (diPadLabel, diPadBox, 0, 1, "DI PAD");
    }
    else if (settingsTab)
    {
        // SETTINGS: GL and OVERSAMPLING, two cells on one row.
        placeDeckSwitch (glLabel, glButton, 0, 1, "GL");
        placeDeckSwitch (oversamplingLabel, oversamplingBox, 1, 1, "OVER");

        // UI SOUNDS takes the third cell of the same row. It is the same KIND of
        // switch as GL and OVERSAMPLING - an engine-level preference rather than
        // a control that shapes the sound - so it belongs beside them rather
        // than on a page of its own. Its caption is carried by the control's own
        // button text, which the in-tab style draws beside the pill, so no
        // Label of its own is needed.
        uiSoundsButton.setBounds (grid.getX() + 2 * cellWidth + 12,
                                  memberRowY + 20,
                                  cellWidth - 24,
                                  juce::jmin (30, grid.getBottom() - memberRowY - 22));
    }
    else if (spaceTab)
    {
        // SPACE: the delay trio rides the member row under the delay knobs.
        placeDeckSwitch (delayTypeLabel, delayTypeBox, 0, 1, "TYPE");
        placeDeckSwitch (delaySyncLabel, delaySyncButton, 1, 1, "SYNC DELAY");
        placeDeckSwitch (delayRateLabel, delayRateBox, 2, 1, "RATE");
    }
    else if (characterTab)
    {
        // CHARACTER: three knobs in the first row, so the member row under them
        // is where the three vinyl voicing selectors live - the order the
        // record is made in: what was cut, what plays it, what reads it.
        placeDeckSwitch (vinylGenerationLabel, vinylGenerationBox, 0, 1, "GENERATION");
        placeDeckSwitch (vinylTurntableLabel, vinylTurntableBox, 1, 1, "TURNTABLE");
        placeDeckSwitch (vinylCartridgeLabel, vinylCartridgeBox, 2, 1, "CARTRIDGE");
    }
    else if (dynamicsTab)
    {
        // DYNAMICS: four knobs fill the first row, so the member row under them
        // is where the neural model picker lives - LOAD and CLEAR side by side
        // in the first two cells, with the status readout beside them. The
        // picker is not a knob (it opens a file), so it is laid out here rather
        // than by the grid.
        const auto buttonY = memberRowY + 20;
        const auto buttonH = juce::jmin (30, grid.getBottom() - memberRowY - 22);

        loadNeuralButton.setBounds (grid.getX() + 12, buttonY,
                                    cellWidth - 24, buttonH);
        clearNeuralButton.setBounds (grid.getX() + cellWidth + 12, buttonY,
                                     cellWidth - 24, buttonH);
        neuralStatusLabel.setBounds (grid.getX() + 2 * cellWidth + 6, buttonY - 2,
                                     2 * cellWidth - 12, buttonH + 4);
    }
    else if (inEqTab || outEqTab)
    {
        // IN EQ / OUT EQ: six knobs fill the first row, so the row below is
        // where the equaliser's three corner controls go - the slope ORDER
        // first, then the two on/off switches for the corners whose frequency
        // knobs are in the row above.
        //
        // The switches are placed by the SAME cell arithmetic as everything
        // else (label band, control band, cell width), through placeEqCorner
        // below, rather than by hand-placed coordinates: the two switches
        // share the row with the ORDER combo, and a coordinate written for one
        // of them would be a guess about the other's.
        const auto isIn = inEqTab;
        placeDeckSwitch (isIn ? inputEqOrderLabel : outputEqOrderLabel,
                         isIn ? inputEqOrderBox : outputEqOrderBox, 0, 1, "ORDER");

        const auto placeEqCorner = [&] (juce::Component& button, int column)
        {
            auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                              memberRowY,
                                              cellWidth, grid.getBottom() - memberRowY);
            button.setBounds (cell.getX() + 12, cell.getY() + 20,
                              cell.getWidth() - 24,
                              juce::jmin (30, cell.getHeight() - 22));
        };

        if (isIn)
        {
            placeEqCorner (inputEqHpButton, 1);
            placeEqCorner (inputEqLpButton, 2);
        }
        else
        {
            placeEqCorner (outputEqHpButton, 1);
            placeEqCorner (outputEqLpButton, 2);
        }
    }

    // Four meters in a 2 x 2 grid inside the meters panel:
    //   row 1 - INPUT and OUTPUT level VU meters
    //   row 2 - COMP IN and COMP OUT gain-reduction meters
    // The rows share the available height evenly, so the arrangement (and the
    // input-left / output-right reading order) holds at any panel size.
    auto meterArea = layout.meters.reduced (12, 0);
    meterArea.removeFromTop (46);           // heading + hint labels, with 4 px clearance
    meterArea.removeFromBottom (8);

    const auto columnGap = 8;
    const auto rowGap = 8;
    const auto columnWidth = (meterArea.getWidth() - columnGap) / 2;

    // Note the separate name: the control grid's row height is taken by the knob
    // arithmetic above (knobRowHeight), so reusing that name here would shadow it
    // through the rest of the function and trip the redefinition error rather
    // than silently picking the wrong cell size.
    const auto meterRowHeight = (meterArea.getHeight() - rowGap) / 2;

    const auto leftColumn = meterArea.getX();
    const auto rightColumn = meterArea.getRight() - columnWidth;
    const auto topRow = meterArea.getY();
    const auto bottomRow = meterArea.getBottom() - meterRowHeight;

    inputMeter.setBounds (leftColumn, topRow, columnWidth, meterRowHeight);
    outputMeter.setBounds (rightColumn, topRow, columnWidth, meterRowHeight);
    compressorMeterIn.setBounds (leftColumn, bottomRow, columnWidth, meterRowHeight);
    compressorMeterOut.setBounds (rightColumn, bottomRow, columnWidth, meterRowHeight);

#if DEBUG
    // ------------------------------------------------------------------
    //  The overlap check.
    //
    //  The panel's most persistent complaint has been furniture printed on top
    //  of furniture - captions over knobs, switches over switches - and it
    //  kept coming back because nothing in the build can see it: every
    //  individual line of a hand-placed layout is a correct rectangle, and the
    //  error lives in the RELATIONSHIP between two of them. Ten pixels is a
    //  real collision and a perfectly reasonable-looking number. It is found
    //  here, in the two places it happened: the header's AUTO GAIN ran into
    //  THEME by ten pixels, and the mode button's own row was the preset row's,
    //  so COMPARE, UNDO, REDO and the badge were printed underneath it.
    //
    //  So the relationship is what gets checked. The furniture is listed BY
    //  NAME rather than swept up from getNumChildComponents(), because the
    //  things that are laid out by arithmetic rather than by hand - the knob
    //  grid, the tab bar, the meters, the vinyl reel - are computed from a
    //  member count and cannot overlap by construction, and sweeping them in
    //  would bury the real signal in a hundred legitimate grid contacts. The
    //  list is the panel's hand-placed surface, and it is the one place a new
    //  switch has to be added to be covered.
    //
    //  It runs LAST in resized(), at the real bounds just set: an item placed
    //  further down the function - the tab grid, the deck switches, the meters
    //  - would otherwise be measured at the bounds it had before this resize,
    //  which is a stale rectangle and reports a collision that is not there.
    //  Opening the editor at the 780 x 816 minimum - where the clearance is
    //  thinnest and where the collisions actually happened - is therefore
    //  enough to fail on the next one, rather than on the next person to open
    //  it on a big screen.
    //
    //  DEBUG only: a quadratic sweep on a path that runs whenever the window
    //  moves is nothing to pay for in a development build and nothing at all
    //  in a release one.
    // ------------------------------------------------------------------
    {
        const struct { const char* name; const juce::Component* component; } furniture[] = {
            // The header: the two switch rows, the theme key and the readout.
            { "brand",         &brandLabel },
            { "title",         &titleLabel },
            { "subtitle",      &subtitleLabel },
            { "status",        &statusLabel },
            { "bypass",        &bypassButton },
            { "delta",         &deltaButton },
            { "polarity",      &polarityButton },
            { "autoGain",      &autoGainButton },
            { "theme",         &themeButton },

            // The deck, line by line, in the order resized() places them.
            { "deckHeading",   &deckHeadingLabel },
            { "build",         &buildLabel },
            { "tapeTypeLabel", &tapeTypeLabel },
            { "tapeTypeBox",   &tapeTypeBox },
            { "speedLabel",    &speedLabel },
            { "speedBox",      &speedBox },
            { "bpmLabel",      &bpmLabel },
            { "bpmReadout",    &bpmReadout },
            { "oversampLabel", &oversamplingLabel },
            { "oversampBox",   &oversamplingBox },
            { "instrLabel",    &instrumentLabel },
            { "instrBox",      &instrumentBox },
            { "gl",            &glButton },
            { "harmonics",     &harmonicsLabel },
            { "harmonicsVal",  &harmonicsReadout },
            { "subfund",       &subfundLabel },
            { "subfundVal",    &subfundReadout },
            { "antiPhase",     &antiPhaseLabel },
            { "antiPhaseVal",  &antiPhaseReadout },
            { "vinylTypeLabel",     &vinylTypeLabel },
            { "vinylTypeBox",       &vinylTypeBox },
            { "vinylSpeedLabel",    &vinylSpeedLabel },
            { "vinylSpeedBox",      &vinylSpeedBox },
            { "valveTypeLabel",     &valveTypeLabel },
            { "valveTypeBox",       &valveTypeBox },
            { "ampTypeLabel",       &ampTypeLabel },
            { "ampTypeBox",         &ampTypeBox },
            { "transformerLabel",   &transformerTypeLabel },
            { "transformerBox",     &transformerTypeBox },
            { "digitalTypeLabel",   &digitalTypeLabel },
            { "digitalTypeBox",     &digitalTypeBox },
            { "transportLabel",     &transportLabel },
            { "spindownLabel",      &spindownLabel },
            { "stop",               &transportStopButton },
            { "play",               &transportPlayButton },
            { "start",              &transportStartButton },
            { "spindown",           &spindownButton },
            { "deckHint",           &deckHintLabel },
            { "mode",               &modeCycleButton },
            { "presetHeading",      &presetHeadingLabel },
            { "presetBox",          &presetBox },
            { "userPresetBox",      &userPresetBox },
            { "savePreset",         &savePresetButton },
            { "deletePreset",       &deletePresetButton },
            { "copyA",              &copyAButton },
            { "copyB",              &copyBButton },
            { "compare",            &compareButton },
            { "undo",               &undoButton },
            { "redo",               &redoButton },
            { "compareBadge",       &compareBadgeLabel },
            { "presetBadge",        &presetBadgeLabel },
        };

        // Only what is on screen counts: a hidden tab member is deliberately
        // placed on top of its neighbours and shown later, one tab at a time.
        std::vector<std::pair<const char*, juce::Rectangle<int>>> placed;

        for (const auto& item : furniture)
        {
            const auto bounds = item.component->getBounds();

            // A zero-sized or invisible item cannot collide with anything a
            // person can see, and the panel uses one-pixel rules as dividers.
            if (! item.component->isVisible() || bounds.getWidth() <= 1 || bounds.getHeight() <= 1)
                continue;

            placed.emplace_back (item.name, bounds);
        }

        for (std::size_t a = 0; a < placed.size(); ++a)
        {
            for (std::size_t b = a + 1; b < placed.size(); ++b)
            {
                const auto& first  = placed[a].second;
                const auto& second = placed[b].second;

                // Touching edges are a row, not a collision, and the panel
                // places several pairs exactly edge to edge on purpose. Only a
                // TRUE overlap - positive area in both axes - is a fault.
                if (first.getRight() <= second.getX() || second.getRight() <= first.getX()
                    || first.getBottom() <= second.getY() || second.getBottom() <= first.getY())
                    continue;

                jassertfalse;
                std::cout << "overlap: " << placed[a].first << " " << first.toString()
                          << "  with  " << placed[b].first << " " << second.toString()
                          << std::endl;
            }
        }
    }
#endif
}
