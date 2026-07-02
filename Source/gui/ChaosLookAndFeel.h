/*
    CHAOS REALM — ChaosLookAndFeel.h
    A dark, neon-accented look for the whole plugin.  Header-only.
*/
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace chaosui
{

// Palette --------------------------------------------------------------------
namespace Colours
{
    const juce::Colour background { 0xff0b0e14 };
    const juce::Colour panel      { 0xff141a24 };
    const juce::Colour panelLight { 0xff1d2430 };
    const juce::Colour text       { 0xffe6ecf5 };
    const juce::Colour textDim    { 0xff8a95a5 };
    const juce::Colour accent     { 0xff00e5c8 }; // teal
    const juce::Colour accent2    { 0xffb14bff }; // violet
    const juce::Colour accent3    { 0xffff3d81 }; // magenta
    const juce::Colour grid       { 0xff222a38 };
}

class ChaosLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChaosLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, Colours::text);
        setColour (juce::ComboBox::backgroundColourId, Colours::panelLight);
        setColour (juce::ComboBox::textColourId, Colours::text);
        setColour (juce::ComboBox::outlineColourId, Colours::grid);
        setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent2.withAlpha (0.4f));
        setColour (juce::ToggleButton::textColourId, Colours::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle  = startAngle + pos * (endAngle - startAngle);
        const float thickness = radius * 0.24f;

        // Track.
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness,
                             0.0f, startAngle, endAngle, true);
        g.setColour (Colours::grid);
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Value arc — colour blends accent->accent3 with value.
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness,
                             0.0f, startAngle, angle, true);
        g.setColour (Colours::accent.interpolatedWith (Colours::accent3, pos));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Pointer.
        juce::Path pointer;
        const float pl = radius * 0.55f;
        pointer.startNewSubPath (centre.x, centre.y);
        pointer.lineTo (centre.x + pl * std::cos (angle - juce::MathConstants<float>::halfPi),
                        centre.y + pl * std::sin (angle - juce::MathConstants<float>::halfPi));
        g.setColour (Colours::text);
        g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        // Hub.
        g.setColour (Colours::panelLight);
        g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre (centre));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool /*shouldDrawHighlighted*/, bool /*down*/) override
    {
        auto b = button.getLocalBounds().toFloat();
        const float h = juce::jmin (18.0f, b.getHeight());
        auto pill = b.removeFromLeft (h * 1.9f).withSizeKeepingCentre (h * 1.9f, h);
        const bool on = button.getToggleState();

        g.setColour (on ? Colours::accent.withAlpha (0.85f) : Colours::grid);
        g.fillRoundedRectangle (pill, h * 0.5f);
        g.setColour (Colours::background);
        auto knob = juce::Rectangle<float> (h - 4.0f, h - 4.0f)
                        .withCentre ({ on ? pill.getRight() - h * 0.5f : pill.getX() + h * 0.5f,
                                       pill.getCentreY() });
        g.setColour (Colours::text);
        g.fillEllipse (knob);

        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), b.reduced (4, 0).toNearestInt(),
                    juce::Justification::centredLeft, true);
    }

    juce::Font getLabelFont (juce::Label&) override { return juce::Font (12.0f); }
};

} // namespace chaosui
