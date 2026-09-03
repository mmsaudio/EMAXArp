#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
EMAXArpAudioProcessor::EMAXArpAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("AudioIn",
                                       juce::AudioChannelSet::stereo(),
                                       true)
                          .withOutput ("AudioOut",
                                       juce::AudioChannelSet::stereo(),
                                       true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    modeParam     = apvts.getRawParameterValue ("mode");
    rateParam     = apvts.getRawParameterValue ("rate");
    gateParam     = apvts.getRawParameterValue ("gate");
    octavesParam  = apvts.getRawParameterValue ("octaves");
    holdParam     = apvts.getRawParameterValue ("hold");
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EMAXArpAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Add further EMAX parameters here, following the same pattern.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Mode",
        juce::StringArray { "As Played", "Up", "Down", "Up / Down", "Random" },
        (int) ArpEngine::Mode::up));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "rate", 1 }, "Rate",
        juce::NormalisableRange<float> (0.25f, 20.0f, 0.01f, 0.3f), 4.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gate", 1 }, "Gate",
        juce::NormalisableRange<float> (0.05f, 1.0f, 0.01f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "octaves", 1 }, "Octaves", 0, 3, 1));

    // Hold: how long the last sequence keeps looping after all keys are
    // released. The very top of the range (10 s) means "forever" until a
    // new note replaces the sequence -- displayed as the infinity sign.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hold", 1 }, "Hold",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f, 0.4f), 2.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                if (value >= 9.999f)
                    return juce::String (juce::CharPointer_UTF8 ("\xE2\x88\x9E")); // infinity
                return juce::String (value, 2) + " s";
            })));

    return layout;
}

//==============================================================================
void EMAXArpAudioProcessor::prepareToPlay (double sampleRate, int)
{
    arp.prepare (sampleRate);
}

void EMAXArpAudioProcessor::releaseResources() {}

void EMAXArpAudioProcessor::reset()
{
    arp.reset();
}

bool EMAXArpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void EMAXArpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    ArpEngine::Params p;
    p.mode    = (int) modeParam->load();
    p.rateHz  = rateParam->load();
    p.gate    = gateParam->load();
    p.octaves = (int) octavesParam->load();

    const float holdValue = holdParam->load();
    p.holdSeconds = holdValue;
    p.holdLatch   = holdValue >= 9.9995f;   // knob at maximum: hold forever

    arp.setParameters (p);

    juce::MidiBuffer out;
    arp.process (midiMessages, out, buffer.getNumSamples());
    midiMessages.swapWith (out);
}

//==============================================================================
juce::AudioProcessorEditor* EMAXArpAudioProcessor::createEditor()
{
    return new EMAXArpAudioProcessorEditor (*this);
}

//==============================================================================
void EMAXArpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EMAXArpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EMAXArpAudioProcessor();
}
