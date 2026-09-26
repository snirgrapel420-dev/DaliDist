#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// עיצוב אישי למראה Neon Purple
class DaliLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DaliLookAndFeel()
    {
        // גווני סגול ניאון
        setColour (juce::Slider::thumbColourId, juce::Colour (0xFFFF00FF)); // סגול-מג'נטה בוהק
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF9D00FF)); // סגול עמוק מואר
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xFF1A0A24)); // רקע כהה לסליידר
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF110518));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xFF9D00FF));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFFFF00FF));
    }
};

class DaliDistAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    DaliDistAudioProcessorEditor (DaliDistAudioProcessor&);
    ~DaliDistAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DaliDistAudioProcessor& audioProcessor;
    DaliLookAndFeel customSkin;

    juce::Slider driveSlider, characterSlider, lowSlider, midSlider, highSlider;
    juce::Slider xLowSlider, xHighSlider, driftSlider, mixSlider, outputSlider;
    
    juce::ComboBox modeBox;
    juce::ToggleButton autoGainButton, bypassButton;

    juce::Label driveLabel, characterLabel, lowLabel, midLabel, highLabel;
    juce::Label xLowLabel, xHighLabel, driftLabel, mixLabel, outputLabel, modeLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> driveAttach, characterAttach, lowAttach, midAttach, highAttach;
    std::unique_ptr<SliderAttachment> xLowAttach, xHighAttach, driftAttach, mixAttach, outputAttach;
    std::unique_ptr<ComboBoxAttachment> modeAttach;
    std::unique_ptr<ButtonAttachment> autoGainAttach, bypassAttach;

    void setupRotary (juce::Slider& slider, juce::Label& label, const juce::String& name);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DaliDistAudioProcessorEditor)
};
