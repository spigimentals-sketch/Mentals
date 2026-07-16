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

# ----------------------------------------------------------------------------
# AI Assist's two trained models (see AiAssistModel.h and Models/README.md),
# embedded the same way MentalsUI embeds mentals_logo.png -- compiled-in
# binary data, no file to ship or locate at runtime.
# ----------------------------------------------------------------------------
juce_add_binary_data(MentalsAutotuneAiAssistData
    HEADER_NAME AiAssistBinaryData.h
    NAMESPACE AiAssistBinaryData
    SOURCES
        Models/ai_assist_regressor.onnx
        Models/ai_assist_classifier.onnx)

target_sources(MentalsAutotune
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp
        AiAssistModel.cpp)

target_compile_definitions(MentalsAutotune
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsAutotune
    PRIVATE
        MentalsUI
        MentalsAutotuneAiAssistData
        onnxruntime
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# onnxruntime.dll is a runtime dependency (imported SHARED library, not
# statically linked), so each built format needs its own copy sitting next
# to the binary -- JUCE creates one target per FORMATS entry above.
# ----------------------------------------------------------------------------
foreach (mentals_autotune_format VST3 Standalone)
    if (TARGET MentalsAutotune_${mentals_autotune_format})
        add_custom_command(TARGET MentalsAutotune_${mentals_autotune_format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:onnxruntime>"
                "$<TARGET_FILE_DIR:MentalsAutotune_${mentals_autotune_format}>")
    endif()
endforeach()
