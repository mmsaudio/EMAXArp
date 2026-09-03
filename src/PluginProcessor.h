#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ArpEngine.h"

//==============================================================================
class EMAXArpAudioProcessor : public juce::AudioProcessor
{
public:
    EMAXArpAudioProcessor();
    ~EMAXArpAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true;  }
    bool producesMidi() const override { return true;  }
    bool isMidiEffect() const override { return true;  }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ArpEngine arp;

    std::atomic<float>* modeParam    = nullptr;
    std::atomic<float>* rateParam    = nullptr;
    std::atomic<float>* gateParam    = nullptr;
    std::atomic<float>* octavesParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EMAXArpAudioProcessor)
};
