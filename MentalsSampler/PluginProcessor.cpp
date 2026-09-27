#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PlaceholderBinaryData.h"

namespace
{
    // Display names/tile colours for the 12 instrument categories -- distinct
    // hues so category tiles are easy to tell apart at a glance. Each
    // category holds MentalsSamplerAudioProcessor::soundsPerCategory
    // individual sounds (see makeSlotName()), placeholder-named until real
    // samples are embedded (see loadEmbeddedSamples()).
    struct CategoryDefaults { const char* name; juce::uint32 colour; };

    const std::array<CategoryDefaults, MentalsSamplerAudioProcessor::numCategories> kCategoryDefaults { {
        { "Brass",     0xff2e9bff }, { "Woodwinds",  0xffe6b800 }, { "Strings", 0xff2ecc71 },
        { "Mallets",   0xffdc143c }, { "Percussion", 0xffff9f1a }, { "Choir",   0xff9b59b6 },
        { "Keys",      0xff1abc9c }, { "Bass",       0xffe74c3c }, { "Synth",   0xff3498db },
        { "Guitar",    0xfff1c40f }, { "Organ",      0xff2ecc71 }, { "Bells",   0xffe67e22 },
    } };

    // One sound's placeholder name within its category, e.g. "Brass 1" --
    // renamed to the real sample's name once loadEmbeddedSamples() loads
    // actual audio into that slot.
    juce::String makeSlotName (int categoryIndex, int soundIndexInCategory)
    {
        return juce::String (kCategoryDefaults[(size_t) categoryIndex].name) + " " + juce::String (soundIndexInCategory + 1);
    }

    // One embedded placeholder tone per category (see PlaceholderSamples/
    // generate_placeholders.py) -- order matches kCategoryDefaults exactly.
    // The same short tone is used for all soundsPerCategory slots within a
    // category for now, purely to exercise the full playback pipeline
    // end-to-end; swapping in real, distinct-per-slot recordings later is a
    // data change here, not a code change.
    struct EmbeddedSound { const char* data; int size; };

    const std::array<EmbeddedSound, MentalsSamplerAudioProcessor::numCategories> kEmbeddedCategorySounds { {
        { PlaceholderBinaryData::Brass_wav,      PlaceholderBinaryData::Brass_wavSize },
        { PlaceholderBinaryData::Woodwinds_wav,  PlaceholderBinaryData::Woodwinds_wavSize },
        { PlaceholderBinaryData::Strings_wav,    PlaceholderBinaryData::Strings_wavSize },
        { PlaceholderBinaryData::Mallets_wav,    PlaceholderBinaryData::Mallets_wavSize },
        { PlaceholderBinaryData::Percussion_wav, PlaceholderBinaryData::Percussion_wavSize },
        { PlaceholderBinaryData::Choir_wav,      PlaceholderBinaryData::Choir_wavSize },
        { PlaceholderBinaryData::Keys_wav,       PlaceholderBinaryData::Keys_wavSize },
        { PlaceholderBinaryData::Bass_wav,       PlaceholderBinaryData::Bass_wavSize },
        { PlaceholderBinaryData::Synth_wav,      PlaceholderBinaryData::Synth_wavSize },
        { PlaceholderBinaryData::Guitar_wav,     PlaceholderBinaryData::Guitar_wavSize },
        { PlaceholderBinaryData::Organ_wav,      PlaceholderBinaryData::Organ_wavSize },
        { PlaceholderBinaryData::Bells_wav,      PlaceholderBinaryData::Bells_wavSize },
    } };
}

//==============================================================================
MentalsSamplerAudioProcessor::MentalsSamplerAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int c = 0; c < numCategories; ++c)
        categories[(size_t) c] = { kCategoryDefaults[(size_t) c].name, juce::Colour (kCategoryDefaults[(size_t) c].colour) };

    for (int i = 0; i < numSlots; ++i)
    {
        const int categoryIndex = categoryIndexForSlot (i);
        slots[(size_t) i].name = makeSlotName (categoryIndex, i - categoryIndex * soundsPerCategory);
        slots[(size_t) i].tileColour = categories[(size_t) categoryIndex].colour;
    }

    selectedSlotParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("selectedSlot"));
    attackParam       = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("attack"));
    releaseParam      = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("release"));
    volumeParam       = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("volume"));
    panParam          = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("pan"));

    loadEmbeddedSamples();
    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsSamplerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray slotNames;
    for (int i = 0; i < numSlots; ++i)
    {
        const int categoryIndex = categoryIndexForSlot (i);
        slotNames.add (makeSlotName (categoryIndex, i - categoryIndex * soundsPerCategory));
    }

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "selectedSlot", "Sound", slotNames, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack",
        juce::NormalisableRange<float> (0.0f, 2000.0f, 1.0f, 0.4f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release",
        juce::NormalisableRange<float> (1.0f, 4000.0f, 1.0f, 0.4f), 200.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "volume", "Volume",
        juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "pan", "Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
// Real audio isn't embedded yet -- this is the single place that will change
// once sample WAVs are added to the project and converted to BinaryData
// (juce_add_binary_data, same mechanism MentalsStereoShaper uses for its AI
// models). Each entry would become something like:
//
//   slots[i].audioData = decodeWavFromBinaryData (BinaryData::mySound_wav, BinaryData::mySound_wavSize);
//   slots[i].sourceSampleRate = <the wav's native rate>;
//   slots[i].name = "My Sound";
//
// Left empty for now so the UI/voice engine can be built and tested first.
//==============================================================================
void MentalsSamplerAudioProcessor::loadEmbeddedSamples()
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    for (int categoryIndex = 0; categoryIndex < numCategories; ++categoryIndex)
    {
        const auto& embedded = kEmbeddedCategorySounds[(size_t) categoryIndex];
        auto stream = std::make_unique<juce::MemoryInputStream> (embedded.data, (size_t) embedded.size, false);
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (std::move (stream)));
        if (reader == nullptr)
            continue;

        juce::AudioBuffer<float> decoded ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&decoded, 0, (int) reader->lengthInSamples, 0, true, true);

        for (int i = 0; i < soundsPerCategory; ++i)
        {
            auto& slot = slots[(size_t) (categoryIndex * soundsPerCategory + i)];
            slot.audioData = decoded; // small buffers (<2s each); fine to duplicate across a category's slots
            slot.sourceSampleRate = reader->sampleRate;
        }
    }
}

//==============================================================================
bool MentalsSamplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MentalsSamplerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (auto& voice : activeVoices)
    {
        voice.active = false;
        voice.envelope.setSampleRate (sampleRate);
    }
}

//==============================================================================
void MentalsSamplerAudioProcessor::startVoice (int slotIndex, int midiNote, float velocity, bool preview)
{
    if (slotIndex < 0 || slotIndex >= numSlots || ! slots[(size_t) slotIndex].hasAudio())
        return;

    // Reuse a free voice slot, or steal the oldest active one for the same
    // note (retriggering) if none are free.
    ActiveVoice* target = nullptr;
    for (auto& v : activeVoices)
    {
        if (! v.active) { target = &v; break; }
    }
    if (target == nullptr)
    {
        for (auto& v : activeVoices)
            if (v.midiNote == midiNote) { target = &v; break; }
    }
    if (target == nullptr)
        target = &activeVoices.front();

    juce::ADSR::Parameters envParams;
    envParams.attack  = juce::jmax (0.001f, attackParam->get() * 0.001f);
    envParams.decay   = 0.001f;
    envParams.sustain = 1.0f;
    envParams.release = juce::jmax (0.001f, releaseParam->get() * 0.001f);

    target->active = true;
    target->slotIndex = slotIndex;
    target->midiNote = midiNote;
    target->readPosition = 0.0;
    target->isInPreviewMode = preview;

    const auto& slot = slots[(size_t) slotIndex];
    const double pitchRatio = std::pow (2.0, (midiNote - 60) / 12.0);
    target->playbackRatio = pitchRatio * (slot.sourceSampleRate / currentSampleRate);

    target->envelope.setParameters (envParams);
    target->envelope.noteOn();
    juce::ignoreUnused (velocity);
}

void MentalsSamplerAudioProcessor::stopVoice (int midiNote)
{
    for (auto& v : activeVoices)
        if (v.active && v.midiNote == midiNote && ! v.isInPreviewMode)
            v.envelope.noteOff();
}

void MentalsSamplerAudioProcessor::triggerPreview (int slotIndex)
{
    startVoice (slotIndex, 60, 1.0f, true);
}

//==============================================================================
void MentalsSamplerAudioProcessor::renderVoices (juce::AudioBuffer<float>& outputBuffer)
{
    const int numOutSamples = outputBuffer.getNumSamples();
    const float volumeGain = juce::Decibels::decibelsToGain (volumeParam->get());
    const float pan = panParam->get();
    const float leftGain  = volumeGain * std::sqrt (0.5f * (1.0f - pan));
    const float rightGain = volumeGain * std::sqrt (0.5f * (1.0f + pan));

    for (auto& voice : activeVoices)
    {
        if (! voice.active)
            continue;

        const auto& slot = slots[(size_t) voice.slotIndex];
        const int sourceLength = slot.audioData.getNumSamples();

        for (int i = 0; i < numOutSamples; ++i)
        {
            const int readIndex = (int) voice.readPosition;
            if (readIndex >= sourceLength)
            {
                voice.active = false;
                break;
            }

            // Linear interpolation -- playbackRatio is 1.0 only when a note
            // is played at exactly middle C with a source sample rate that
            // matches the session; every other note pitch-shifts via a
            // non-integer step, so nearest-neighbour reads would otherwise
            // add audible aliasing/graininess to anything but middle C.
            const float frac = (float) (voice.readPosition - (double) readIndex);
            const int nextIndex = juce::jmin (readIndex + 1, sourceLength - 1);
            const float s0 = slot.audioData.getSample (0, readIndex);
            const float s1 = slot.audioData.getSample (0, nextIndex);

            const float envValue = voice.envelope.getNextSample();
            const float sample = s0 + frac * (s1 - s0);
            const float shaped = sample * envValue;

            outputBuffer.addSample (0, i, shaped * leftGain);
            if (outputBuffer.getNumChannels() > 1)
                outputBuffer.addSample (1, i, shaped * rightGain);

            voice.readPosition += voice.playbackRatio;

            if (! voice.envelope.isActive())
            {
                voice.active = false;
                break;
            }
        }
    }
}

void MentalsSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            startVoice (getSelectedSlot(), message.getNoteNumber(), message.getFloatVelocity(), false);
        else if (message.isNoteOff())
            stopVoice (message.getNoteNumber());
    }

    renderVoices (buffer);
    updateOutputLevelMeter (buffer);
}

void MentalsSamplerAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float releasePerBlock = std::pow (10.0f, -24.0f * ((float) buffer.getNumSamples() / (float) currentSampleRate) / 20.0f);
    const float previous = outputPeakLinear.load();
    outputPeakLinear.store (juce::jmax (peak, previous * releasePerBlock));

    if (peak >= 1.0f)
        clipHoldBlocksRemaining.store ((int) (1.5 * currentSampleRate / juce::jmax (1, buffer.getNumSamples())));
    else if (clipHoldBlocksRemaining.load() > 0)
        clipHoldBlocksRemaining.fetch_sub (1);
}

//==============================================================================
juce::AudioProcessorEditor* MentalsSamplerAudioProcessor::createEditor()
{
    return new MentalsSamplerAudioProcessorEditor (*this);
}

//==============================================================================
void MentalsSamplerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsSamplerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
void MentalsSamplerAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    resetToDefault();
    presetManager.savePreset ("Default");
}
