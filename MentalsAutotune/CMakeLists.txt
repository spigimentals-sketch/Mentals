# ----------------------------------------------------------------------------
# Core library: the actual processor/editor code, in its own static library
# target so Mentals Suite can link it directly and host this same
# AudioProcessor as one stage of its internal chain, without duplicating any
# DSP, the AI Assist model, or rebuilding this plugin's UI.
# ----------------------------------------------------------------------------
juce_add_binary_data(MentalsAutotuneAiAssistData
    HEADER_NAME AiAssistBinaryData.h
    NAMESPACE AiAssistBinaryData
    SOURCES
        Models/ai_assist_regressor.onnx
        Models/ai_assist_classifier.onnx)

add_library(MentalsAutotuneCore STATIC
    PluginProcessor.cpp
    PluginEditor.cpp
    AiAssistModel.cpp)

# Core libraries aren't created via juce_add_plugin/juce_add_gui_app, so they
# need this internal JUCE CMake helper called directly to become "JUCE-aware"
# (it's what those juce_add_* functions call themselves) -- otherwise
# juce_generate_juce_header() below has nowhere to generate JuceHeader.h.
_juce_initialise_target(MentalsAutotuneCore)

target_include_directories(MentalsAutotuneCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

juce_generate_juce_header(MentalsAutotuneCore)

target_compile_definitions(MentalsAutotuneCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsAutotuneCore
    PUBLIC
        MentalsUI
        MentalsAutotuneAiAssistData
        onnxruntime
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# The standalone/VST3/AU plugin: just wraps MentalsAutotuneCore in JUCE's
# plugin formats.
# ----------------------------------------------------------------------------
juce_add_plugin(MentalsAutotune
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
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
        CreatePluginFilter.cpp)

target_link_libraries(MentalsAutotune
    PRIVATE
        MentalsAutotuneCore)

# ----------------------------------------------------------------------------
# onnxruntime.dll/.dylib is a runtime dependency (imported SHARED library,
# not statically linked), so each built format needs its own copy sitting
# next to the binary -- JUCE creates one target per FORMATS entry above.
# AU is included alongside VST3/Standalone since it's a real target on
# macOS (the only platform AU builds on) and was previously missing here,
# which would have left the AU component without the dylib it needs.
#
# On macOS the copy alone isn't enough: the dynamic loader also needs to be
# told to look next to the executable for @rpath-referenced libraries, so
# BUILD_RPATH is set to @loader_path there too -- harmless on top of
# whatever install name the vendored dylib itself already has.
# ----------------------------------------------------------------------------
foreach (mentals_autotune_format VST3 Standalone AU)
    if (TARGET MentalsAutotune_${mentals_autotune_format})
        add_custom_command(TARGET MentalsAutotune_${mentals_autotune_format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:onnxruntime>"
                "$<TARGET_FILE_DIR:MentalsAutotune_${mentals_autotune_format}>")
        if (APPLE)
            set_target_properties(MentalsAutotune_${mentals_autotune_format} PROPERTIES
                BUILD_RPATH "@loader_path")
        endif()
    endif()
endforeach()
