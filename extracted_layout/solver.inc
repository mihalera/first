// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/layout/extract.py.
constexpr int knobTileChrome = 46;
    // Between two rows of tiles. A gap is air the rows are separated by, and it is
    // charged to the knobs of every row - so it is the smallest one that still
    // separates two lines of type: 8 px here leaves the readout of the row above
    // and the caption band of the row below 14 px apart, where the 14 px gap this
    // was cost the height-capped pages a further 1.4 px of disc (MACHINE at
    // 1060 x 916 measures 70.2 with it and 68.8 without).
    constexpr int knobRowGap = 8;
    const auto rowsForColumns = [knobCount] (int columns)
    {
        return (knobCount + columns - 1) / columns;
    };
    // The camera the CANDIDATES are scored at. The score compares counts within
    // one page - the same band of the same panel - so a mid value is all it needs
    // to be: the wall moves by a few hundredths across the strip and moves every
    // candidate the same way.
    constexpr float knobScoringWall = 0.4f;

    // The counts are walked from the MOST rows to the fewest (one column is n rows
    // by definition, so fewer columns means more rows), and a count only displaces
    // the one held if it buys a disc a twentieth larger: a page settles on more
    // rows when the panel is tall enough for that to mean bigger knobs, and stays
    // on the single row otherwise rather than splitting itself for nothing - a
    // strip that flipped between the two arrangements on a few pixels of window
    // resize would be worse than either.
    // At each step only the count where the ROW COUNT CHANGES is scored: within
    // one row count every further column is a narrower slot, so that count is the
    // widest slot the row count has, and the only one it has to answer with.
    // The score is the disc computeKnobMetrics would give the tile - the same
    // function the knob itself is drawn from, so the layout cannot promise a disc
    // the paint routine then fails to draw.
    auto knobColumns = juce::jmax (1, knobCount);
    auto knobRows = 0;
    auto knobTileWidth = 0;
    auto knobTileHeight = 0;
    auto knobScore = 0.0f;
    auto scoredRows = 0;
    for (int columns = 1; columns <= knobCount; ++columns)
    {
        const auto rows = rowsForColumns (columns);
        if (rows == scoredRows)
            continue;

        scoredRows = rows;
        // What this count offers: the slot width it would give each tile, and the
        // height one row of `rows` has to be drawn in.
        const auto slotWidth = grid.getWidth() / columns;
        const auto rowHeight = (knobAreaHeight - (rows - 1) * knobRowGap) / rows;
        const auto paintWidth = slotWidth - 22;

        if (paintWidth < 4 || rowHeight <= knobTileChrome)
            continue;

        // The tile's HEIGHT is solved, not derived: it is the smallest height at
        // which the cap stops growing. On a big knob that height is the square
        // one - what caps the cap is the face ceiling, and a taller paint area
        // buys nothing past it. On a small knob the groove and its marks bind
        // first, and a taller paint area goes on buying radius until the ring's
        // own ceiling is reached, several pixels past square. Solving it this way
        // is what the old grid was giving away on exactly the pages the user saw
        // as cramped - and it keeps the readout under the knob as close to it as
        // the cap allows, on the pages where it does not cost anything either.
        auto tileHeight = knobTileChrome + 1;
        auto disc = 0.0f;
        for (int candidate = knobTileChrome + 1; candidate <= rowHeight; ++candidate)
        {
            const auto radius = computeKnobMetrics (static_cast<float> (paintWidth),
                                                    static_cast<float> (candidate - knobTileChrome),
                                                    knobScoringWall).radius;
            if (radius > disc)
            {
                disc = radius;
                tileHeight = candidate;
            }
        }

        if (disc > knobScore * 1.04f)
        {
            knobScore = disc;
            knobColumns = columns;
            knobRows = rows;
            knobTileWidth = slotWidth;
            knobTileHeight = tileHeight;
        }
    }
    // Nothing a panel at its own minimum size allows can make this zero, but a
    // strip with no tile would place its knobs at negative coordinates: fall back
    // to one row of the widest slots the grid's width alone can give.
    if (knobTileHeight < 1)
    {
        knobRows = knobCount > 0 ? 1 : 0;
        knobColumns = juce::jmax (1, knobCount);
        knobTileWidth = juce::jmax (1, grid.getWidth() / juce::jmax (1, knobCount));
        knobTileHeight = knobTileWidth + 24;
    }
    const auto knobStripHeight = knobRows > 0
                                     ? knobRows * knobTileHeight + (knobRows - 1) * knobRowGap
                                     : 0;
    const auto knobStripTop = grid.getY()
                              + juce::jmax (0, (knobAreaHeight - knobStripHeight) / 2);
    const auto knobSlotWidth = knobColumns > 0 ? grid.getWidth() / knobColumns : grid.getWidth();
