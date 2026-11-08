// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/layout/extract.py.
int comboTextInset (int fieldHeight, int panelHeight) noexcept
    {
        const auto fieldShare = juce::roundToInt (static_cast<float> (fieldHeight) * 0.12f);
        const auto panelShare = juce::roundToInt (static_cast<float> (panelHeight) * 0.004f);

        return juce::jmax (comboTextMinimumInset, fieldShare + panelShare);
    }
