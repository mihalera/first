// =============================================================================
//  The panel's page table: what each tab is called, which control ids it holds
//  and which of the three families hosts it - the table, and the three lookups
//  built on it (controlIsInTab, index_of_tab_named, tabsCoverAllControls).
//
//  Moved out of PluginEditor.cpp with the paint kit and split from it on purpose:
//  this is the panel's LAYOUT, Source/GUI/PanelPaint.h is what draws the panel,
//  and nothing in either header needs the other.
//
//  Two things are load-bearing and must not be "tidied":
//
//    * `tabSpecs` stays a constexpr array defined HERE rather than a declaration
//      in a .cpp. setCurrentTab() static_asserts against it - its size, both
//      counts, the family of every page - and a static_assert needs the
//      definition, not a promise of one.
//    * The definitions are `inline` for the reason stated in PanelPaint.h: these
//      used to be one copy per translation unit with internal linkage.
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstring>

namespace j37panel
{
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
        int family;                 // which of the three families hosts the page
        std::size_t controls[16];
    };

    // Eleven pages under three large families - TAPE, FX, SETUP - because the
    // panel carries fifty-nine knobs and no single subject should own a crowded
    // page. Walking the bar left to right inside a family still walks the signal:
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
    // on SETTINGS, and the delay TYPE / RATE / SYNC trio shows on DELAY. They are
    // not knobs - the grid below cannot place them - so their visibility is
    // managed in setCurrentTab beside the knobs'.
    //  The sixteen pages are grouped into THREE FAMILIES - TAPE, FX, SETUP - and
    //  no page carries more than one subject, or more than one row of seven
    //  knobs: DRIVE once held twelve and NOISE eleven, which is exactly the
    //  crowding the tabs exist to avoid. The families are the three large
    //  buttons ABOVE this bar (see setTabFamily); the column in each row below
    //  is the page's family, and it is the single source the strip reads.
    //  Sixteen now: MIX is a page of its own, split from SUBFUND because ten
    //  knobs on one page is the crowding this table exists to prevent.
    //
    //  The page count is written ONCE, here, ABOVE the first array that needs
    //  it: the table below, the subtitle list and the coverage check further
    //  down all size themselves from this constant, so adding a page cannot
    //  leave one of them behind.
    inline constexpr std::size_t numTabPages = 18;
    inline constexpr std::array<TabSpec, numTabPages> tabSpecs { {
        // ---- TAPE family (9 pages): the machine and everything feeding it ---
        //  input, tone (BRIGHT), character (TONE), mix, stereo_width, output
        { "MACHINE", "What goes in, how the machine colours it, and what comes out.",
                     7, 0, { 0, 3, 4, 7, 9, 8, 37 } },
        //  di, di_load, di_transformer, preamp, distortion - the gain stages
        //  IN FRONT of the tape. The DI pad/load combo rides this page beside
        //  the knob row in resized(), with the DI it belongs to.
        { "FRONT END", "The gain stages in front of the tape: DISTORT and PREAMP "
                       "push the front end, and the DI loads, pads and colours "
                       "the instrument before any of it - with its PAD and LOAD "
                       "selector beside the row.",
                     5, 0, { 49, 50, 51, 26, 27 } },
        //  drive, bias - the head's magnetic saturation itself.
        { "SATURATION", "The magnetic saturation the head applies: DRIVE sets how "
                        "hard the tape is pushed and BIAS where its operating "
                        "point sits - the two classic recorder controls, on a "
                        "page of their own.",
                     2, 0, { 1, 2 } },
        //  flux, cabinet, presence. The vinyl stage's three voicing selectors
        //  - GENERATION / TURNTABLE / CARTRIDGE - are COMBO BOXES rather than
        //  knobs (the grid only places knobs), so like GL and OVERSAMPLING they
        //  are tab members whose visibility setCurrentTab asserts and resized()
        //  lays out, under the knobs here; the NEURAL model picker takes the
        //  band's fourth column.
        { "CHARACTER", "The head and the medium's tone: FLUX drives the head, "
                       "CABINET and PRESENCE place the machine's voice. The "
                       "record's GENERATION / TURNTABLE / CARTRIDGE re-voice the "
                       "whole vinyl stage from under this row, and the NEURAL "
                       "model picker takes the band's last column.",
                     3, 0, { 28, 19, 20 } },
        //  noise, noise_lvl, wow, flutter, wear, mechanics - the MACHINE's own
        //  departures from a clean signal. The record's five FAULTS are on the
        //  RECORD page: each is a mechanism of the RECORD's noise, not the
        //  machine's.
        { "NOISE", "The machine's own departures from a clean signal. NOISE MIX "
                   "sets how much of it is in the output and NOISE LVL how loud "
                   "the sources run; WOW and FLUTTER are the transport's noise, "
                   "WEAR and MECHANICS the medium's and the mechanism's. The "
                   "record's five FAULTS are on the RECORD page.",
                     6, 0, { 24, 36, 5, 6, 29, 30 } },
        //  The record's five FAULTS, on a page of their own because each IS a
        //  mechanism of the record's own noise: surface texture, a repeating
        //  wound, the platter's warp, the cartridge's earthing, the pressing's
        //  clicks.
        { "RECORD", "The record's five FAULTS: DUST the surface's fine texture, "
                    "SCRATCH a wound crossed once per revolution, WARP the level "
                    "breathing at the platter rate, ELECTRICAL the cartridge's "
                    "earthing, CLICKS the pressing's sharp faults.",
                     5, 0, { 38, 39, 40, 41, 42 } },
        //  The vinyl stage's own three knobs. These are here and NOT also on
        //  NOISE: they are the record's CHARACTER, while the faults are its
        //  noise - listing them on both pages would put the same knob on two
        //  tabs, and the grid can only place one of them.
        { "VINYL", "The record-playing stage: VINYL MIX is how much of it is in "
                   "the output, CRACKLE the surface's granular texture and RUMBLE "
                   "the platter's own low thump. GENERATION / TURNTABLE / "
                   "CARTRIDGE re-voice it all from CHARACTER; the FAULTS are on "
                   "RECORD.",
                     3, 0, { 33, 34, 35 } },
        //  The seven SOURCE knobs alone: the saturation mix IS the page, so the
        //  user dialling TAPE up against VALVE is looking at the whole mix and
        //  nothing else. The mix bar in the band under the row shows the same
        //  seven shares the core is actually running.
        { "MIX", "The saturation mix: seven SOURCE knobs, one per machine. TAPE, "
                 "VALVE, CASSETTE, VINYL, AMP, TRANSFORMER and DIGITAL each set "
                 "that machine's share of the character, and the shares are "
                 "normalised to sum to 100 percent - so what is dialled is what "
                 "is heard, and the bar under the row shows the running mix. A "
                 "machine whose own TYPE is OFF contributes nothing and its "
                 "share is redistributed among the rest.",
                     7, 0, { 10, 11, 12, 13, 14, 15, 16 } },
        //  amp_bias, sag, subfund - everything that BENDS the curve once the
        //  gain stages have pushed it. Three knobs that share one idea (the
        //  curve after the mix), so they are a page of three rather than three
        //  more rows on the mix's.
        { "SUBFUND", "The curve-benders: AMP BIAS moves the amp stage's operating "
                     "point, SAG lets its supply yield under load, and SUBFUND "
                     "generates the octave below what plays.",
                     3, 0, { 17, 18, 25 } },
        // ---- FX family (4 pages): the second head, the room, the envelope,
        //  the output equaliser ------------------------------------------------
        //  delay_time, delay_feedback, st_offset, ping_pong. The deck's TYPE /
        //  SYNC DELAY / RATE switches belong to this page and ride the band
        //  under the knobs.
        { "DELAY", "The second head: DELAY and DLY LVL set the echo, ST OFFSET "
                   "skews the sides against each other, PING-PONG bounces it "
                   "between them. The deck's TYPE / SYNC DELAY / RATE switches "
                   "sit under the row.",
                     4, 1, { 21, 22, 23, 58 } },
        { "REVERB", "The room: REVERB is how much of it is in the output and RVB "
                    "SIZE how large the space the reflections build.",
                     2, 1, { 31, 32 } },
        //  transient_attack (54), transient_sustain (55), transient_mix (56).
        //  The stage that acts on the finished signal's envelope rather than on
        //  the waveform - it changes how the sound MOVES without adding a
        //  harmonic.
        { "DYN", "The stage that shapes the FINISHED signal's envelope. ATK "
                 "sharpens (positive) or softens (negative) the attack of each "
                 "event; SUS lengthens (positive) or shortens (negative) what "
                 "follows the attack, its body and ring; TR MIX is how much of "
                 "the shaped signal reaches the output.",
                     3, 1, { 59, 60, 61 } },
        //  out_low, out_mid, out_high and their three corners.
        { "OUT EQ", "The output equaliser, after the machine and before the output "
                    "trim. Nothing downstream responds to what it does, so it "
                    "corrects the RESULT rather than the input - the neutral, "
                    "predictable EQ you use to place the finished sound. Same "
                    "three bands as the input EQ, same transparency at 0 dB; the "
                    "ORDER and corner switches ride the row below.",
                     6, 1, { 46, 47, 48, 55, 56, 57 } },
        // ---- SETUP family (3 pages): what the machine hears, the file-fed
        //  shapers, the engine switches ----------------------------------------
        //  in_low, in_mid, in_high and their three corners.
        { "IN EQ", "The input equaliser, in front of the machine. What it shapes "
                   "is what the tape HEARS, so lifting the low end here drives "
                   "the saturation curve and the glue compressors harder - it "
                   "changes the character of the processing, not merely the "
                   "balance. LOW is a shelf at 200 Hz, MID a bell at 1 kHz, HIGH "
                   "a shelf above 4 kHz; all three are transparent at 0 dB, and "
                   "the ORDER and corner switches ride the row below.",
                     6, 2, { 43, 44, 45, 52, 53, 54 } },
        //  neural_mix, ir_mix - the two file-fed stages, each with its own
        //  loader in the band under the row.
        { "SHAPERS", "The two file-fed stages: NEURAL blends in a learned model "
                     "and IR MIX an impulse response - a cabinet or a room - "
                     "each loaded from a file through the loader under its own "
                     "knob.",
                     2, 2, { 62, 63 } },
        //  No knobs of its own: SETTINGS is where the three engine-level switches
        //  live - GL, OVERSAMPLING and the interface sounds - shown by
        //  setCurrentTab, not by the grid.
             { "SETTINGS", "The engine-level switches and musical setup: rate, language and tuning reference.",
                               0, 2, {} },
             { "AUTOTUNE", "Pitch correction with safe realtime tracking and scale quantisation.",
                               0, 1, {} },
             { "COMP", "The conventional compressor: threshold, ratio, attack, release, makeup and mix. Independent of both glue stages.",
                               0, 1, {} }
        } };

    // One SHORT subtitle per page, in tabSpecs' order: the sentence the section
    // caption under the tab bar shows. tabSpecs' own description is the long form
    // for the tooltip, and a paragraph of it above a row of knobs is a paragraph
    // nobody reads; this is the one line that says what the page is FOR, which is
    // what fifteen pages of bare knobs were missing.
    //
    // A second array rather than a seventh field on TabSpec, because TabSpec is
    // brace-initialised sixteen times and a field that has to be retyped on every
    // edit is a field that will be forgotten. The static_assert beside
    // tabsCoverAllControls in setCurrentTab keeps the two the same length, so
    // adding a page without a caption is a build error rather than a blank line.
    inline constexpr std::array<const char*, numTabPages> tabSubtitles { {
        "in, out and the stereo field of the whole machine",
        "the gain stages in front of the tape: DI, preamp, distortion",
        "the head's own saturation: DRIVE and where BIAS sits",
        "the head's drive and the machine's two voicing controls",
        "the machine's own departures from a clean signal",
        "the record's five faults, one page each of its own",
        "the record-playing stage: how much, how rough, how low",
        "seven machines, seven shares, one character",
        "the curve-benders: bias, supply sag, and the octave below",
        "the second head: time, repeats, offset and where they land",
        "the room: how much of it is in the output, and how big",
        "the finished signal's envelope, not its waveform",
        "the equaliser after the machine: it corrects the RESULT",
        "the equaliser before the machine: it changes the CHARACTER",
        "the two file-fed stages, each with its loader beside it",
        "the engine-level switches and musical tuning",
        "pitch correction and scale quantisation",
        "the conventional compressor, independent of glue"
    } };

    // Where the divider under the knob-grid heading sits, in pixels from the top of the
    // panel. The tab bar lives between the heading and this line, so paint() and
    // resized() both read it from here: it used to be a literal 42 px in paint(), which
    // is precisely where the first tab button now is.
    inline constexpr int controlsDividerOffset = 92;

    // Does control `index` belong to `tab`?
    inline bool controlIsInTab (std::size_t index, int tab)
    {
        const auto& spec = tabSpecs[static_cast<std::size_t> (tab)];

        for (std::size_t i = 0; i < spec.count; ++i)
            if (spec.controls[i] == index)
                return true;

        return false;
    }

    // Compile-time proof that the tabs partition the grid: every control index appears
    // in exactly one tab, so no knob is orphaned (invisible and unreachable) or shown
    // twice, and none of them is silently dropped. The page count this is sized from
    // is declared above the tab table, where the first array that needs it lives.

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
    inline int index_of_tab_named (const char* name)
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

}
