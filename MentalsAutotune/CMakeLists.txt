juce_add_plugin(MentalsAutotune
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Mtun
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals Autotune")

juce_generate_juce_header(MentalsAutotune)

target_sources(MentalsAutotune
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp)

target_compile_definitions(MentalsAutotune
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsAutotune
    PRIVATE
        MentalsUI
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
