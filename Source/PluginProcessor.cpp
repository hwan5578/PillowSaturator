#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pid = pillow::params::id;

PillowSaturatorAudioProcessor::PillowSaturatorAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PillowSaturator", pillow::params::createLayout())
{
    pInput       = apvts.getRawParameterValue (pid::input);
    pDrive       = apvts.getRawParameterValue (pid::drive);
    pStyle       = apvts.getRawParameterValue (pid::style);
    pCharacter   = apvts.getRawParameterValue (pid::character);
    pTone        = apvts.getRawParameterValue (pid::tone);
    pLowCut      = apvts.getRawParameterValue (pid::lowCut);
    pHighCut     = apvts.getRawParameterValue (pid::highCut);
    pMix         = apvts.getRawParameterValue (pid::mix);
    pOutput      = apvts.getRawParameterValue (pid::output);
    pAutoGain    = apvts.getRawParameterValue (pid::autoGain);
    pQuality     = apvts.getRawParameterValue (pid::quality);
    pLinearPhase = apvts.getRawParameterValue (pid::linearPhase);
    pStereoMode  = apvts.getRawParameterValue (pid::stereoMode);
}

//==============================================================================
bool PillowSaturatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void PillowSaturatorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock); // the engine works in fixed internal chunks

    engine.prepare (sampleRate, juce::jlimit (1, 2, getTotalNumOutputChannels()));
    engine.setParameters (readParameters(), true);
    engine.reset();

    setLatencySamples (engine.getLatencySamples());
    oversamplingFactor.store (engine.getOversamplingFactor());
}

pillow::dsp::EngineParams PillowSaturatorAudioProcessor::readParameters() const noexcept
{
    pillow::dsp::EngineParams p;
    p.inputDb     = pInput->load();
    p.driveDb     = pDrive->load();
    p.style       = juce::roundToInt (pStyle->load());
    p.character   = pCharacter->load() * 0.01f;
    p.tone        = pTone->load() * 0.01f;
    p.lowCutHz    = pLowCut->load();
    p.highCutHz   = pHighCut->load();
    p.mix         = pMix->load() * 0.01f;
    p.outputDb    = pOutput->load();
    p.autoGain    = pAutoGain->load() > 0.5f;
    p.quality     = juce::roundToInt (pQuality->load());
    p.linearPhase = pLinearPhase->load() > 0.5f;
    p.stereoMode  = juce::roundToInt (pStereoMode->load());
    return p;
}

void PillowSaturatorAudioProcessor::storePeak (std::atomic<float>& target, float value) noexcept
{
    if (value > target.load (std::memory_order_relaxed))
        target.store (value, std::memory_order_relaxed);
}

void PillowSaturatorAudioProcessor::process (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0)
        return;

    auto params = readParameters();
    params.bypass = bypassed;
    engine.setParameters (params);

    storePeak (inputPeak, buffer.getMagnitude (0, numSamples));
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), numSamples);
    storePeak (outputPeak, buffer.getMagnitude (0, numSamples));

    // Latency only changes when Linear Phase is toggled (all qualities share one latency).
    const int latency = engine.getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);

    oversamplingFactor.store (engine.getOversamplingFactor(), std::memory_order_relaxed);
}

void PillowSaturatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

// Called by the wrapper while the host bypasses the plug-in: the engine crossfades to the
// latency-compensated dry signal, so bypassing neither clicks nor shifts timing.
void PillowSaturatorAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

//==============================================================================
juce::AudioProcessorEditor* PillowSaturatorAudioProcessor::createEditor()
{
    return new PillowSaturatorAudioProcessorEditor (*this);
}

void PillowSaturatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PillowSaturatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PillowSaturatorAudioProcessor();
}
