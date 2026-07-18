# ----------------------------------------------------------------------------
# Mentals Suite: hosts any number of Mentals plugins chained together inside
# one instance, reusing each one's own AudioProcessor/Editor unmodified via a
# juce::AudioProcessorGraph (see PluginProcessor.h) -- so it links every
# other plugin's *Core library directly rather than duplicating any DSP.
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
        PluginProcessor.cpp
        PluginEditor.cpp
        MasterAssistant.cpp)

target_compile_definitions(MentalsSuite
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsSuite
    PRIVATE
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
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

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
