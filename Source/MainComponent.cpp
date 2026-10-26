#include "MainComponent.h"

MainComponent::MainComponent()
{
    titleLabel.setText ("Roadie", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::Font (32.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    setSize (600, 400);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    titleLabel.setBounds (getLocalBounds());
}
