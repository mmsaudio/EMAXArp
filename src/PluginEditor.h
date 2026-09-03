#pragma once

#include "PluginProcessor.h"

//==============================================================================
class EMAXArpAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit EMAXArpAudioProcessorEditor (EMAXArpAudioProcessor&);
    ~EMAXArpAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::ComboBox modeBox;
    juce::Label modeLabel    { {}, "Mode" };
    juce::Label rateLabel    { {}, "Rate" };
    juce::Label gateLabel    { {}, "Gate" };
    juce::Label octavesLabel { {}, "Octaves" };
    juce::Label holdLabel    { {}, "Hold" };

    juce::Slider rateSlider;
    juce::Slider gateSlider;
    juce::Slider octavesSlider;
    juce::Slider holdSlider;

    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ComboAtt>  modeAttachment;
    std::unique_ptr<SliderAtt> rateAttachment, gateAttachment, octavesAttachment, holdAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EMAXArpAudioProcessorEditor)
};
