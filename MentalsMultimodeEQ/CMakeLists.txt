# ----------------------------------------------------------------------------
# Core library: the actual processor/editor code, in its own static library
# target so Mentals Suite can link it directly and host this same
# AudioProcessor as one stage of its internal chain, without duplicating any
# DSP, the AI Assist models, or rebuilding this plugin's UI.
# ----------------------------------------------------------------------------
juce_add_binary_data(MultiModeEQBinaryData
    HEADER_NAME MultiModeEQBinaryData.h
    NAMESPACE MultiModeEQBinaryData
    SOURCES
        logo.png)

juce_add_binary_data(MultiModeEQAiAssistData
    HEADER_NAME EqAssistBinaryData.h
    NAMESPACE EqAssistBinaryData
    SOURCES
        Models/eq_assist_resonance.onnx
        Models/eq_assist_lowcut_classifier.onnx
        Models/eq_assist_highcut_classifier.onnx
        Models/eq_assist_lowcut_freq.onnx
        Models/eq_assist_highcut_freq.onnx
        Models/eq_assist_category.onnx)

add_library(MultiModeEQCore STATIC
    PluginProcessor.cpp
    PluginEditor.cpp
    EqAssistModel.cpp
    EqMixRegistry.cpp)

# Core libraries aren't created via juce_add_plugin/juce_add_gui_app, so they
# need this internal JUCE CMake helper called directly to become "JUCE-aware"
# (it's what those juce_add_* functions call themselves) -- otherwise
# juce_generate_juce_header() below has nowhere to generate JuceHeader.h.
_juce_initialise_target(MultiModeEQCore)

target_include_directories(MultiModeEQCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

juce_generate_juce_header(MultiModeEQCore)

target_compile_definitions(MultiModeEQCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # MP3's core patents expired in 2017; enabled so EQ Match's "Load
        # Reference..." can actually read .mp3 files (JUCE ships this reader
        # disabled by default). WAV/AIFF/FLAC/OGG already work without this.
        JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(MultiModeEQCore
    PUBLIC
        MultiModeEQBinaryData
        MultiModeEQAiAssistData
        MentalsUI
        onnxruntime
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# The standalone/VST3/AU plugin: just wraps MultiModeEQCore in JUCE's plugin
# formats.
# ----------------------------------------------------------------------------
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
    PRODUCT_NAME "Mentals Parametric EQ")

juce_generate_juce_header(MultiModeEQ)

target_sources(MultiModeEQ
    PRIVATE
        CreatePluginFilter.cpp)

target_link_libraries(MultiModeEQ
    PRIVATE
        MultiModeEQCore)

# ----------------------------------------------------------------------------
# onnxruntime.dll/.dylib is a runtime dependency (imported SHARED library,
# not statically linked), so each built format needs its own copy sitting
# next to the binary -- JUCE creates one target per FORMATS entry above.
# Mirrors Autotune's/Stereo Shaper's own copy of this same loop.
#
# On macOS the copy alone isn't enough: the dynamic loader also needs to be
# told to look next to the executable for @rpath-referenced libraries, so
# BUILD_RPATH is set to @loader_path there too -- harmless on top of
# whatever install name the vendored dylib itself already has.
# ----------------------------------------------------------------------------
foreach (multimode_eq_format VST3 Standalone AU)
    if (TARGET MultiModeEQ_${multimode_eq_format})
        add_custom_command(TARGET MultiModeEQ_${multimode_eq_format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:onnxruntime>"
                "$<TARGET_FILE_DIR:MultiModeEQ_${multimode_eq_format}>")
        if (APPLE)
            set_target_properties(MultiModeEQ_${multimode_eq_format} PROPERTIES
                BUILD_RPATH "@loader_path")
        endif()
    endif()
endforeach()
