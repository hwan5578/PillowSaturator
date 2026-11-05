#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/SaturatorEngine.h"

class PillowSaturatorAudioProcessor final : public juce::AudioProcessor
{
public:
    PillowSaturatorAudioProcessor();
    ~PillowSaturatorAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor (message thread); peak since the last read.
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<int> oversamplingFactor { 4 };

private:
    void process (juce::AudioBuffer<float>& buffer, bool bypassed);
    pillow::dsp::EngineParams readParameters() const noexcept;
    static void storePeak (std::atomic<float>& target, float value) noexcept;

    pillow::dsp::SaturatorEngine engine;

    std::atomic<float>* pInput = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pStyle = nullptr;
    std::atomic<float>* pCharacter = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pHighCut = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pAutoGain = nullptr;
    std::atomic<float>* pQuality = nullptr;
    std::atomic<float>* pLinearPhase = nullptr;
    std::atomic<float>* pStereoMode = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PillowSaturatorAudioProcessor)
};
