#include "PluginEditor.h"

namespace
{
    const juce::Colour background   (0xff1e1e22);
    const juce::Colour panel        (0xff2a2a30);
    const juce::Colour text         (0xffd0d0d0);
    const juce::Colour accent       (0xff7aa2d8);
    const juce::Colour outline      (0xff44444c);
}

//==============================================================================
EMAXArpAudioProcessorEditor::EMAXArpAudioProcessorEditor (EMAXArpAudioProcessor& p)
    : AudioProcessorEditor (p)
{
    // --- mode combo ----------------------------------------------------------
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("mode")))
        modeBox.addItemList (choice->getAllValueStrings(), 1);

    modeBox.setColour (juce::ComboBox::backgroundColourId, panel);
    modeBox.setColour (juce::ComboBox::textColourId, text);
    modeBox.setColour (juce::ComboBox::outlineColourId, outline);
    modeBox.setColour (juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<ComboAtt> (p.apvts, "mode", modeBox);

    // --- sliders -------------------------------------------------------------
    for (auto* slider : { &rateSlider, &gateSlider, &octavesSlider, &holdSlider })
    {
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        slider->setColour (juce::Slider::backgroundColourId, panel);
        slider->setColour (juce::Slider::trackColourId, accent.withAlpha (0.4f));
        slider->setColour (juce::Slider::thumbColourId, accent);
        slider->setColour (juce::Slider::textBoxTextColourId, text);
        slider->setColour (juce::Slider::textBoxBackgroundColourId, panel);
        slider->setColour (juce::Slider::textBoxOutlineColourId, outline);
        addAndMakeVisible (slider);
    }

    rateAttachment    = std::make_unique<SliderAtt> (p.apvts, "rate", rateSlider);
    gateAttachment    = std::make_unique<SliderAtt> (p.apvts, "gate", gateSlider);
    octavesAttachment = std::make_unique<SliderAtt> (p.apvts, "octaves", octavesSlider);
    holdAttachment    = std::make_unique<SliderAtt> (p.apvts, "hold", holdSlider);

    // --- labels --------------------------------------------------------------
    for (auto* label : { &modeLabel, &rateLabel, &gateLabel, &octavesLabel, &holdLabel })
    {
        label->setFont (juce::Font (juce::FontOptions (14.0f)));
        label->setColour (juce::Label::textColourId, text);
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    setSize (440, 330);
}

//==============================================================================
void EMAXArpAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText ("EMAX Arp", getLocalBounds().removeFromTop (36), juce::Justification::centred);
}

void EMAXArpAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (28);   // title area

    auto placeRow = [&] (juce::Component& label, juce::Component& control)
    {
        auto row = bounds.removeFromTop (48);
        label.setBounds (row.removeFromLeft (80));
        control.setBounds (row);
    };

    placeRow (modeLabel, modeBox);
    placeRow (rateLabel, rateSlider);
    placeRow (gateLabel, gateSlider);
    placeRow (octavesLabel, octavesSlider);
    placeRow (holdLabel, holdSlider);
}