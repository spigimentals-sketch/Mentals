#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
// Shared colour palette for every Mentals plugin: charcoal/slate core with
// electric-blue selection, golden curve accents, and a dedicated green/amber/
// red scale reserved for meters. Kept identical across plugins so they read
// as one product family.
//==============================================================================
namespace MentalsUI
{
    namespace Colours
    {
        // ---- Core palette --------------------------------------------------------
        const juce::Colour charcoalBlack { 0xff1a1a1a }; // dominant background
        const juce::Colour slateGray     { 0xff44494f }; // panels, borders, inactive controls
        const juce::Colour slateGrayDark { 0xff26292d }; // recessed panels (graph/text-box backgrounds)
        const juce::Colour white         { 0xfff2f2f2 }; // text labels and values

        // ---- Accent colours --------------------------------------------------------
        const juce::Colour electricBlue  { 0xff2e9bff }; // active module/band selection
        const juce::Colour goldenYellow  { 0xffe6b800 }; // curve accents
        const juce::Colour emeraldGreen  { 0xff2ecc71 }; // safe levels in meters
        const juce::Colour amberOrange   { 0xffff9f1a }; // caution zone in meters
        const juce::Colour crimsonRed    { 0xffdc143c }; // clipping indicator
    }
}
