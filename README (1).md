# Mentals Multimode EQ (JUCE, VST3 + AU)

A single-band EQ plugin with three switchable modes:

- **Parametric** — Frequency, Gain, Q all adjustable.
- **Semi-Parametric** — Frequency and Gain adjustable, Q fixed internally.
- **Dynamic** — Parametric controls plus Threshold, Ratio, Attack, Release;
  the band's gain is automatically reduced as the input level (in that
  band) rises above the threshold, like a frequency-selective compressor.

See the accompanying chat message for the full build/test/extend guide.

## Layout
```
MultiModeEQ/
  CMakeLists.txt
  Source/
    PluginProcessor.h / .cpp   -- DSP + parameters
    PluginEditor.h   / .cpp    -- GUI
  JUCE/                        -- (you add this: JUCE framework checkout)
```
