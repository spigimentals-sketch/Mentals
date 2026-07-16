cmake_minimum_required(VERSION 3.22)

project(MultiModeEQ VERSION 1.0.0)

# ----------------------------------------------------------------------------
# JUCE is pulled in as a submodule/subdirectory. See the build guide for how
# to obtain it (either `git clone` or `FetchContent`).
# ----------------------------------------------------------------------------
add_subdirectory(JUCE)

juce_add_plugin(MultiModeEQ
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Mmeq
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals Multimode EQ")

juce_generate_juce_header(MultiModeEQ)

# ----------------------------------------------------------------------------
# Embeds logo.png as a compiled-in resource (BinaryData::logo_png /
# BinaryData::logo_pngSize) rather than loading it from a path on disk, so the
# plugin doesn't depend on any file existing next to the built binary.
# ----------------------------------------------------------------------------
juce_add_binary_data(MultiModeEQBinaryData
    SOURCES
        logo.png)

target_sources(MultiModeEQ
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp)

target_compile_definitions(MultiModeEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # MP3's core patents expired in 2017; enabled so EQ Match's "Load
        # Reference..." can actually read .mp3 files (JUCE ships this reader
        # disabled by default). WAV/AIFF/FLAC/OGG already work without this.
        JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(MultiModeEQ
    PRIVATE
        MultiModeEQBinaryData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
