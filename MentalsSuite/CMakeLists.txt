# ----------------------------------------------------------------------------
# Core library: the actual chain-hosting processor/editor code, in its own
# static library target so Mentals Live Host can link it directly and host
# one MentalsSuiteAudioProcessor per live mixer channel as that channel's
# freely user-configurable insert chain, without duplicating any of this
# chain-hosting logic or rebuilding this plugin's own "+Add Module" UI.
#
# Hosts any number of Mentals plugins chained together inside one instance,
# reusing each one's own AudioProcessor/Editor unmodified via a
# juce::AudioProcessorGraph (see PluginProcessor.h) -- so it links every
# other plugin's *Core library directly rather than duplicating any DSP.
# ----------------------------------------------------------------------------
add_library(MentalsSuiteCore STATIC
    PluginProcessor.cpp
    PluginEditor.cpp
    MasterAssistant.cpp)

_juce_initialise_target(MentalsSuiteCore)

target_include_directories(MentalsSuiteCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

juce_generate_juce_header(MentalsSuiteCore)

target_compile_definitions(MentalsSuiteCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsSuiteCore
    PUBLIC
        MentalsUI
        MultiModeEQCore
        MentalsDeEsserCore
        MentalsCompressorCore
        MentalsSaturatorCore
        MentalsAutotuneCore
        MentalsDelayCore
        MentalsReverbCore
        MentalsLimiterCore
        MentalsGateCore
        MentalsChorusCore
        MentalsVoxChoirCore
        MentalsStereoShaperCore
        MentalsExciterEQCore
        MentalsCircuitCompCore
        MentalsMasteringMeterCore
        MentalsChannelStripCore
        MentalsMaximizerCore
        MentalsDoublerCore
        MentalsImagerCore
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# The standalone/VST3/AU plugin: just wraps MentalsSuiteCore in JUCE's
# plugin formats.
# ----------------------------------------------------------------------------
juce_add_plugin(MentalsSuite
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Msui
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals Suite")

juce_generate_juce_header(MentalsSuite)

target_sources(MentalsSuite
    PRIVATE
        CreatePluginFilter.cpp)

target_link_libraries(MentalsSuite
    PRIVATE
        MentalsSuiteCore)

# ----------------------------------------------------------------------------
# onnxruntime.dll/.dylib is a runtime dependency of MentalsAutotuneCore/
# MentalsStereoShaperCore (Autotune's AI Assist, Stereo Shaper's AI
# Placement), pulled in transitively here -- same copy step as their own
# CMakeLists.txt files. AU is included alongside VST3/Standalone since it's
# a real target on macOS (the only platform AU builds on) and was
# previously missing here, which would have left Suite's AU component
# without the dylib it needs whenever it constructs one of those two
# modules.
#
# On macOS the copy alone isn't enough: the dynamic loader also needs to be
# told to look next to the executable for @rpath-referenced libraries, so
# BUILD_RPATH is set to @loader_path there too -- harmless on top of
# whatever install name the vendored dylib itself already has.
# ----------------------------------------------------------------------------
foreach (mentals_suite_format VST3 Standalone AU)
    if (TARGET MentalsSuite_${mentals_suite_format})
        add_custom_command(TARGET MentalsSuite_${mentals_suite_format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:onnxruntime>"
                "$<TARGET_FILE_DIR:MentalsSuite_${mentals_suite_format}>")
        if (APPLE)
            set_target_properties(MentalsSuite_${mentals_suite_format} PROPERTIES
                BUILD_RPATH "@loader_path")
        endif()
    endif()
endforeach()
