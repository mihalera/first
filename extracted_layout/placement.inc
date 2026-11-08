// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/layout/extract.py.
for (int slot = 0; slot < knobCount; ++slot)
    {
        // Which widget this slot IS. The tab table holds every knob page's
        // controls and nothing else, so a page's slot is an index into it - but
        // COMP's six and AUTOTUNE's six are the widgets the table cannot hold
        // (they belong to their page's own machinery rather than to a knob page),
        // so those two pages' slots address their label and slider directly.
        const auto slotIndex = static_cast<std::size_t> (slot);
        auto& label = compressorTab ? compressorControlLabels[slotIndex]
                                    : autotuneTab ? autoTuneControlLabels[slotIndex]
                                                  : controlLabels[activeTab.controls[slotIndex]];
        auto& control = compressorTab ? compressorControls[slotIndex]
                                      : autotuneTab ? autoTuneControls[slotIndex]
                                                    : controls[activeTab.controls[slotIndex]];
        const auto row = slot / knobColumns;
        const auto column = slot % knobColumns;
        // The tiles ride EQUAL SLOTS rather than sitting edge to edge, so a page
        // whose discs the panel's height capped keeps its knobs spread across the
        // panel instead of stacking them against the left edge with all the air
        // on the right - and a last row with fewer knobs than the one above it is
        // centred as a group under it.
        const auto slotsInRow = juce::jmin (knobColumns, knobCount - row * knobColumns);
        const auto rowLeft = grid.getX() + (grid.getWidth() - slotsInRow * knobSlotWidth) / 2;
        const auto tileLeft = rowLeft + column * knobSlotWidth
                              + (knobSlotWidth - knobTileWidth) / 2;
        auto cell = juce::Rectangle<int> (tileLeft,
                                          knobStripTop + row * (knobTileHeight + knobRowGap),
                                          knobTileWidth, knobTileHeight);
        label.setBounds (cell.getX() + 5, cell.getY() + 1,
                         cell.getWidth() - 10, 17);
        auto sliderBounds = cell.reduced (5);
        // 8 px of clearance under the caption, not the 18 this used to reserve.
        // The caption's own band is 17 px tall and is laid out above this cell,
        // so the extra 10 px were never caption clearance at all - they were
        // taken off the knob, and the knob's drawing area is what is left here
        // after the readout band. The ticks stop short of the top of that area
        // (see scaleBand in drawRotarySlider), so the scale ends where this
        // clearance begins and nothing touches the caption.
        sliderBounds.removeFromTop (8);
        control.setBounds (sliderBounds);
    }
