#include "PluginProcessor.h"
#include "PluginEditor.h"

DaliDistAudioProcessorEditor::DaliDistAudioProcessorEditor (DaliDistAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&customSkin);

    setupRotary (driveSlider, driveLabel, "Drive");
    setupRotary (characterSlider, characterLabel, "Character");
    setupRotary (lowSlider, lowLabel, "Low Color");
    setupRotary (midSlider, midLabel, "Mid Color");
    setupRotary (highSlider, highLabel, "High Color");
    setupRotary (xLowSlider, xLowLabel, "Low / Mid");
    setupRotary (xHighSlider, xHighLabel, "Mid / High");
    setupRotary (driftSlider, driftLabel, "Drift");
    setupRotary (mixSlider, mixLabel, "Mix");
    setupRotary (outputSlider, outputLabel, "Output");

    addAndMakeVisible (modeBox);
    modeBox.addItemList ({"Tube", "Transformer", "Tape", "Console", "Acid"}, 1);
    addAndMakeVisible (modeLabel);
    modeLabel.setText ("Mode", juce::dontSendNotification);
    modeLabel.attachToComponent (&modeBox, false);
    modeLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (autoGainButton);
    autoGainButton.setButtonText ("Auto Gain");
    
    addAndMakeVisible (bypassButton);
    bypassButton.setButtonText ("Bypass");

    driveAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::drive, driveSlider);
    characterAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::character, characterSlider);
    lowAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::low, lowSlider);
    midAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::mid, midSlider);
    highAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::high, highSlider);
    xLowAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::xLow, xLowSlider);
    xHighAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::xHigh, xHighSlider);
    driftAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::drift, driftSlider);
    mixAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::mix, mixSlider);
    outputAttach = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::output, outputSlider);
    
    modeAttach = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, ParamIDs::mode, modeBox);
    autoGainAttach = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::autoGain, autoGainButton);
    bypassAttach = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::bypass, bypassButton);

    setSize (800, 500);
}

DaliDistAudioProcessorEditor::~DaliDistAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void DaliDistAudioProcessorEditor::setupRotary (juce::Slider& slider, juce::Label& label, const juce::String& name)
{
    addAndMakeVisible (slider);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    
    addAndMakeVisible (label);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);
}

void DaliDistAudioProcessorEditor::paint (juce::Graphics& g)
{
    // רקע בגווני סגול-שחור כהים
    juce::ColourGradient bgGradient (juce::Colour (0xFF0A0010), 0, 0, juce::Colour (0xFF1A002A), 0, (float) getHeight(), false);
    g.setGradientFill (bgGradient);
    g.fillAll();

    // כותרת הפלאגין בצד ימין למעלה
    g.setColour (juce::Colour (0xFFFF00FF)); // סגול-מג'נטה
    g.setFont (juce::Font (32.0f, juce::Font::bold));
    g.drawText ("DALI DISTORTION", getWidth() - 320, 20, 300, 40, juce::Justification::right, true);
    
    g.setColour (juce::Colour (0xFF9D00FF));
    g.setFont (20.0f);
    g.drawText ("Analog Color Box", getWidth() - 320, 50, 300, 30, juce::Justification::right, true);
}

void DaliDistAudioProcessorEditor::resized()
{
    const int startY = 120;
    const int knobSize = 100;
    const int spacing = 140;

    // שורה עליונה הועברה שמאלה כדי לפנות מקום לכותרת בימין
    modeBox.setBounds (20, 30, 150, 30);
    autoGainButton.setBounds (190, 35, 100, 20);
    bypassButton.setBounds (300, 35, 100, 20);

    // שורה אמצעית
    driveSlider.setBounds (50, startY, knobSize, knobSize);
    characterSlider.setBounds (50 + spacing, startY, knobSize, knobSize);
    driftSlider.setBounds (50 + spacing * 2, startY, knobSize, knobSize);
    mixSlider.setBounds (50 + spacing * 3, startY, knobSize, knobSize);
    outputSlider.setBounds (50 + spacing * 4, startY, knobSize, knobSize);

    // שורה תחתונה
    int eqY = startY + 160;
    lowSlider.setBounds (50, eqY, knobSize, knobSize);
    xLowSlider.setBounds (50 + spacing, eqY, knobSize, knobSize);
    midSlider.setBounds (50 + spacing * 2, eqY, knobSize, knobSize);
    xHighSlider.setBounds (50 + spacing * 3, eqY, knobSize, knobSize);
    highSlider.setBounds (50 + spacing * 4, eqY, knobSize, knobSize);
}
