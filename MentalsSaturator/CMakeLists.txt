juce_add_plugin(MentalsSaturator
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Msat
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals Saturator")

juce_generate_juce_header(MentalsSaturator)

target_sources(MentalsSaturator
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp)

target_compile_definitions(MentalsSaturator
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsSaturator
    PRIVATE
        MentalsUI
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
