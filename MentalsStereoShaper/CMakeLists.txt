# ----------------------------------------------------------------------------
# Core library: the actual processor/editor code, in its own static library
# target so Mentals Suite can link it directly and host this same
# AudioProcessor as one stage of its internal chain, without duplicating any
# DSP, the AI Placement model, or rebuilding this plugin's UI.
# ----------------------------------------------------------------------------
juce_add_binary_data(MentalsStereoShaperPlacementData
    HEADER_NAME PlacementBinaryData.h
    NAMESPACE PlacementBinaryData
    SOURCES
        Models/mix_placement_rotation.onnx
        Models/mix_placement_width.onnx)

add_library(MentalsStereoShaperCore STATIC
    PluginProcessor.cpp
    PluginEditor.cpp
    MixRegistry.cpp
    PlacementModel.cpp)

# Core libraries aren't created via juce_add_plugin/juce_add_gui_app, so they
# need this internal JUCE CMake helper called directly to become "JUCE-aware"
# (it's what those juce_add_* functions call themselves) -- otherwise
# juce_generate_juce_header() below has nowhere to generate JuceHeader.h.
_juce_initialise_target(MentalsStereoShaperCore)

target_include_directories(MentalsStereoShaperCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

juce_generate_juce_header(MentalsStereoShaperCore)

target_compile_definitions(MentalsStereoShaperCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MentalsStereoShaperCore
    PUBLIC
        MentalsUI
        MentalsStereoShaperPlacementData
        onnxruntime
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# The standalone/VST3/AU plugin: just wraps MentalsStereoShaperCore in JUCE's
# plugin formats.
# ----------------------------------------------------------------------------
juce_add_plugin(MentalsStereoShaper
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Mstr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Mentals 360 Stereo Shaper")

juce_generate_juce_header(MentalsStereoShaper)

target_sources(MentalsStereoShaper
    PRIVATE
        CreatePluginFilter.cpp)

target_link_libraries(MentalsStereoShaper
    PRIVATE
        MentalsStereoShaperCore)

# ----------------------------------------------------------------------------
# onnxruntime.dll is a runtime dependency (imported SHARED library, not
# statically linked), so each built format needs its own copy sitting next
# to the binary -- JUCE creates one target per FORMATS entry above.
# ----------------------------------------------------------------------------
foreach (mentals_stereo_shaper_format VST3 Standalone)
    if (TARGET MentalsStereoShaper_${mentals_stereo_shaper_format})
        add_custom_command(TARGET MentalsStereoShaper_${mentals_stereo_shaper_format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:onnxruntime>"
                "$<TARGET_FILE_DIR:MentalsStereoShaper_${mentals_stereo_shaper_format}>")
    endif()
endforeach()
