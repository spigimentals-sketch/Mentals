# ----------------------------------------------------------------------------
# Core library: the actual processor/editor code, in its own static library
# target so Mentals Suite can link it directly and host this same
# AudioProcessor as one stage of its internal chain, without duplicating any
# DSP or rebuilding this plugin's UI.
# ----------------------------------------------------------------------------
add_library(MentalsReverbCore STATIC
    PluginProcessor.cpp
    PluginEditor.cpp)

# Core libraries aren't created via juce_add_plugin/juce_add_gui_app, so they
# need this internal JUCE CMake helper called directly to become "JUCE-aware"
# (it's what those juce_add_* functions call themselves) -- otherwise
# juce_generate_juce_header() below has nowhere to generate JuceHeader.h.
_juce_initialise_target(MentalsReverbCore)

target_include_directories(MentalsReverbCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

juce_generate_juce_header(MentalsReverbCore)

target_compile_definitions(MentalsReverbCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsReverbCore
    PUBLIC
        MentalsUI
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# The standalone/VST3/AU plugin: just wraps MentalsReverbCore in JUCE's plugin
# formats.
# ----------------------------------------------------------------------------
juce_add_plugin(MentalsReverb
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Mrev
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals Reverb")

juce_generate_juce_header(MentalsReverb)

target_sources(MentalsReverb
    PRIVATE
        CreatePluginFilter.cpp)

target_link_libraries(MentalsReverb
    PRIVATE
        MentalsReverbCore)
