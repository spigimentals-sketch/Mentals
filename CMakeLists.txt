cmake_minimum_required(VERSION 3.22)

project(MentalsPlugins VERSION 1.0.0)

# ----------------------------------------------------------------------------
# JUCE is pulled in once here as a submodule/subdirectory, shared by every
# plugin subproject below.
# ----------------------------------------------------------------------------
add_subdirectory(JUCE)

# ----------------------------------------------------------------------------
# ONNX Runtime, vendored as a prebuilt binary distribution (ThirdParty/
# onnxruntime) rather than built from source -- it's the inference engine
# behind Mentals Autotune's AI Assist model (see
# MentalsAutotune/AiAssistModel.h), and since that model's input is a
# 3-number feature vector rather than raw audio, the model itself is tiny
# and this dependency is only ever asked to run a few floats through a
# couple of small RandomForest graphs.
#
# Not committed to the repo (same as JUCE/ -- see .gitignore): re-extract
# the platform-matching release asset from
# https://github.com/microsoft/onnxruntime/releases (this was set up
# against v1.27.1) into ThirdParty/onnxruntime/, keeping its include/ and
# lib/ subfolders -- "onnxruntime-win-x64-<version>.zip" on Windows,
# "onnxruntime-osx-universal2-<version>.tgz" on macOS (see
# .github/workflows/build-macos.yml, which fetches the latter
# automatically for CI). The .pdb/.dSYM debug symbol files aren't needed
# and were deleted to save space (some 380MB+ each on Windows).
# ----------------------------------------------------------------------------
add_library(onnxruntime SHARED IMPORTED GLOBAL)
if (WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/ThirdParty/onnxruntime/lib/onnxruntime.dll"
        IMPORTED_IMPLIB   "${CMAKE_SOURCE_DIR}/ThirdParty/onnxruntime/lib/onnxruntime.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/ThirdParty/onnxruntime/include")
elseif (APPLE)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/ThirdParty/onnxruntime/lib/libonnxruntime.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/ThirdParty/onnxruntime/include")
else()
    message(FATAL_ERROR "No vendored ONNX Runtime binary configured for this platform yet.")
endif()

# ----------------------------------------------------------------------------
# Shared UI library: the colour palette, level meter, splitter bar, labelled
# knob, popup-launcher and preset-manager helpers used by every Mentals
# plugin, so they share one consistent look/behaviour instead of each
# reimplementing the same editor scaffolding. See Shared/MentalsUI for
# details.
#
# Mentals Multimode EQ predates this library and keeps its own local copies
# of the equivalent pieces rather than being migrated onto it -- that would
# be a pure refactor of an already-shipped, tested plugin with no behaviour
# change, so it's left alone. Every plugin added after this point uses
# MentalsUI directly.
# ----------------------------------------------------------------------------
add_subdirectory(Shared/MentalsUI)

# ----------------------------------------------------------------------------
# Individual plugins. Each is its own juce_add_plugin() target in its own
# subfolder -- see that subfolder's CMakeLists.txt for details.
# ----------------------------------------------------------------------------
add_subdirectory(MentalsMultimodeEQ)
add_subdirectory(MentalsDelay)
add_subdirectory(MentalsReverb)
add_subdirectory(MentalsSaturator)
add_subdirectory(MentalsCompressor)
add_subdirectory(MentalsDeEsser)
add_subdirectory(MentalsAutotune)
add_subdirectory(MentalsLimiter)
add_subdirectory(MentalsGate)
add_subdirectory(MentalsChorus)
add_subdirectory(MentalsVoxChoir)
add_subdirectory(MentalsStereoShaper)
add_subdirectory(MentalsExciterEQ)
add_subdirectory(MentalsCircuitComp)
add_subdirectory(MentalsMasteringMeter)
add_subdirectory(MentalsChannelStrip)
add_subdirectory(MentalsMaximizer)
add_subdirectory(MentalsDoubler)
add_subdirectory(MentalsImager)
add_subdirectory(MentalsSampler)
add_subdirectory(MentalsTrigger)

# ----------------------------------------------------------------------------
# Mentals Suite chains the plugins above together in one instance -- added
# after them since it links each of their *Core library targets, which must
# already exist.
# ----------------------------------------------------------------------------
add_subdirectory(MentalsSuite)

# ----------------------------------------------------------------------------
# Mentals Live Host hosts one MentalsSuiteAudioProcessor per live mixer
# channel as that channel's insert chain -- added last since it links
# MentalsSuiteCore, which must already exist.
# ----------------------------------------------------------------------------
add_subdirectory(MentalsLiveHost)
