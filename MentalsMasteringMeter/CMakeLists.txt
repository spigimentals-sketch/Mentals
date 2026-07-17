# ----------------------------------------------------------------------------
# Core library: the actual processor/editor code, in its own static library
# target so Mentals Suite can link it directly and host this same
# AudioProcessor as one stage of its internal chain, without duplicating any
# DSP or rebuilding this plugin's UI.
# ----------------------------------------------------------------------------
add_library(MentalsMasteringMeterCore STATIC
    PluginProcessor.cpp
    PluginEditor.cpp)

# Core libraries aren't created via juce_add_plugin/juce_add_gui_app, so they
# need this internal JUCE CMake helper called directly to become "JUCE-aware"
# (it's what those juce_add_* functions call themselves) -- otherwise
# juce_generate_juce_header() below has nowhere to generate JuceHeader.h.
_juce_initialise_target(MentalsMasteringMeterCore)

target_include_directories(MentalsMasteringMeterCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

juce_generate_juce_header(MentalsMasteringMeterCore)

target_compile_definitions(MentalsMasteringMeterCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsMasteringMeterCore
    PUBLIC
        MentalsUI
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# The standalone/VST3/AU plugin: just wraps MentalsMasteringMeterCore in
# JUCE's plugin formats.
# ----------------------------------------------------------------------------
juce_add_plugin(MentalsMasteringMeter
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Mmtr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals Mastering Meter")

juce_generate_juce_header(MentalsMasteringMeter)

target_sources(MentalsMasteringMeter
    PRIVATE
        CreatePluginFilter.cpp)

target_link_libraries(MentalsMasteringMeter
    PRIVATE
        MentalsMasteringMeterCore)
