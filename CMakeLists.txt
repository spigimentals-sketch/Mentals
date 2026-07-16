cmake_minimum_required(VERSION 3.22)

project(MentalsPlugins VERSION 1.0.0)

# ----------------------------------------------------------------------------
# JUCE is pulled in once here as a submodule/subdirectory, shared by every
# plugin subproject below.
# ----------------------------------------------------------------------------
add_subdirectory(JUCE)

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
