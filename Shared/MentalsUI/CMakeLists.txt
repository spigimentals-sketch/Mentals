# ----------------------------------------------------------------------------
# Embeds the shared "MENTALS" wordmark (cropped from Mentals Multimode EQ's
# original logo, with the product-specific subtitle removed) as
# BinaryData::mentals_logo_png, so every plugin can show it without depending
# on a file next to the built binary.
# ----------------------------------------------------------------------------
juce_add_binary_data(MentalsSharedBinaryData
    SOURCES
        mentals_logo.png)

add_library(MentalsUI STATIC
    LevelMeterComponent.cpp
    GainReductionMeterComponent.cpp
    VuMeterComponent.cpp
    MentalsLookAndFeel.cpp)

target_include_directories(MentalsUI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MentalsUI
    PUBLIC
        MentalsSharedBinaryData
        juce::juce_gui_basics
        juce::juce_audio_processors
        juce::juce_audio_utils)
