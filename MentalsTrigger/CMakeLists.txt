add_library(MentalsTriggerCore STATIC
    PluginProcessor.cpp
    PluginEditor.cpp)

_juce_initialise_target(MentalsTriggerCore)

target_include_directories(MentalsTriggerCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

juce_generate_juce_header(MentalsTriggerCore)

target_compile_definitions(MentalsTriggerCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsTriggerCore
    PUBLIC
        MentalsUI
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_plugin(MentalsTrigger
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Mtrg
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals Trigger")

juce_generate_juce_header(MentalsTrigger)

target_sources(MentalsTrigger
    PRIVATE
        CreatePluginFilter.cpp)

target_link_libraries(MentalsTrigger
    PRIVATE
        MentalsTriggerCore)
