/*
    CHAOS REALM — PluginEditor.cpp
*/
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
namespace C = chaosui::Colours;

//==============================================================================
//  SpectrumAnalyzer
//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (ChaosRealmAudioProcessor& p) : processor (p)
{
    scratch.assign (kFFTSize, 0.0f);
    fftData.assign ((size_t) kFFTSize * 2, 0.0f);
    magnitudes.assign (kFFTSize / 2, 0.0f);
    startTimerHz (30);
}

SpectrumAnalyzer::~SpectrumAnalyzer() { stopTimer(); }

void SpectrumAnalyzer::timerCallback()
{
    processor.copyScope (scratch.data(), kFFTSize);
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < kFFTSize; ++i) fftData[(size_t) i] = scratch[(size_t) i];
    window.multiplyWithWindowingTable (fftData.data(), kFFTSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 2.0f / (float) kFFTSize;
    for (int i = 0; i < kFFTSize / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * norm + 1.0e-9f);
        const float level = juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, 0.0f, 1.0f);
        // Smooth decay for a fluid display.
        magnitudes[(size_t) i] = juce::jmax (level, magnitudes[(size_t) i] * 0.82f);
    }
    repaint();
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (C::panel);
    g.fillRoundedRectangle (b, 6.0f);

    // Grid lines (log frequency decades).
    g.setColour (C::grid);
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = b.getX() + b.getWidth() * std::log10 (f / 20.0f) / std::log10 (20000.0f / 20.0f);
        g.drawVerticalLine ((int) x, b.getY(), b.getBottom());
    }

    // Spectrum curve.
    juce::Path curve;
    curve.startNewSubPath (b.getX(), b.getBottom());
    const int bins = kFFTSize / 2;
    for (int i = 1; i < bins; ++i)
    {
        const float freq = (float) i * 24000.0f / (float) bins;
        if (freq < 20.0f) continue;
        const float x = b.getX() + b.getWidth() * std::log10 (freq / 20.0f) / std::log10 (20000.0f / 20.0f);
        const float y = b.getBottom() - magnitudes[(size_t) i] * b.getHeight();
        curve.lineTo (x, y);
    }
    curve.lineTo (b.getRight(), b.getBottom());
    curve.closeSubPath();

    juce::ColourGradient grad (C::accent.withAlpha (0.65f), b.getX(), b.getY(),
                               C::accent2.withAlpha (0.15f), b.getX(), b.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (curve);
    g.setColour (C::accent);
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    g.setColour (C::textDim);
    g.setFont (11.0f);
    g.drawText ("SPECTRUM", b.reduced (6.0f).toNearestInt(), juce::Justification::topRight);
}

//==============================================================================
//  ModulePanel
//==============================================================================
ModulePanel::ModulePanel (ChaosRealmAudioProcessor& p, int moduleIndex)
    : processor (p), index (moduleIndex)
{
    auto& m = processor.getEngine().module (index);
    moduleName = juce::String (m.getName());
    const juce::String tag = "m" + juce::String (index);

    enableButton.setButtonText (moduleName);
    addAndMakeVisible (enableButton);
    enableAtt = std::make_unique<APVTS::ButtonAttachment> (processor.getAPVTS(), tag + "_on", enableButton);

    mixSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);
    addAndMakeVisible (mixSlider);
    mixLabel.setText ("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType (juce::Justification::centred);
    mixLabel.setFont (11.0f);
    addAndMakeVisible (mixLabel);
    mixAtt = std::make_unique<APVTS::SliderAttachment> (processor.getAPVTS(), tag + "_mix", mixSlider);

    for (int pi = 0; pi < m.getNumParameters(); ++pi)
    {
        auto* s = new juce::Slider();
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
        addAndMakeVisible (s);
        paramSliders.add (s);

        auto* l = new juce::Label();
        l->setText (juce::String (m.getParameterInfo (pi).name.c_str()), juce::dontSendNotification);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (11.0f);
        addAndMakeVisible (l);
        paramLabels.add (l);

        paramAtts.add (new APVTS::SliderAttachment (
            processor.getAPVTS(), tag + "_p" + juce::String (pi), *s));
    }
}

int ModulePanel::preferredHeight() const noexcept { return 130; }

void ModulePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f);
    const bool on = processor.getEngine().isModuleEnabled (index);
    g.setColour (on ? C::panelLight : C::panel);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (on ? C::accent.withAlpha (0.5f) : C::grid);
    g.drawRoundedRectangle (b, 8.0f, 1.2f);

    // Accent index tab down the left edge.
    const auto accents = std::array<juce::Colour, 3> { C::accent, C::accent2, C::accent3 };
    g.setColour (accents[(size_t) (index % 3)].withAlpha (on ? 0.9f : 0.35f));
    g.fillRoundedRectangle (b.getX() + 2.0f, b.getY() + 6.0f, 4.0f, b.getHeight() - 12.0f, 2.0f);
}

void ModulePanel::resized()
{
    auto b = getLocalBounds().reduced (12, 8);
    auto header = b.removeFromTop (24);
    enableButton.setBounds (header.removeFromLeft (240));

    b.removeFromTop (4);
    const int knobW = 66, knobH = b.getHeight();
    auto row = b;

    auto place = [&] (juce::Slider& s, juce::Label& l)
    {
        auto cell = row.removeFromLeft (knobW);
        l.setBounds (cell.removeFromTop (14));
        s.setBounds (cell.reduced (2, 0));
    };
    place (mixSlider, mixLabel);
    row.removeFromLeft (8);
    for (int i = 0; i < paramSliders.size(); ++i)
    {
        if (row.getWidth() < knobW) break;
        place (*paramSliders[i], *paramLabels[i]);
    }
    juce::ignoreUnused (knobH);
}

//==============================================================================
//  Editor
//==============================================================================
ChaosRealmAudioProcessorEditor::ChaosRealmAudioProcessorEditor (ChaosRealmAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), analyzer (p)
{
    setLookAndFeel (&lookAndFeel);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (11.0f);
        addAndMakeVisible (l);
    };
    setupKnob (inGain, inGainLbl, "In");
    setupKnob (outGain, outGainLbl, "Out");
    setupKnob (masterMix, masterMixLbl, "Master");

    inAtt  = std::make_unique<APVTS::SliderAttachment> (processor.getAPVTS(), "in_gain", inGain);
    outAtt = std::make_unique<APVTS::SliderAttachment> (processor.getAPVTS(), "out_gain", outGain);
    mixAtt = std::make_unique<APVTS::SliderAttachment> (processor.getAPVTS(), "master_mix", masterMix);

    oversampleBox.addItemList ({ "1x", "2x", "4x", "8x", "16x" }, 1);
    addAndMakeVisible (oversampleBox);
    oversampleLbl.setText ("Oversample", juce::dontSendNotification);
    oversampleLbl.setFont (11.0f);
    oversampleLbl.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (oversampleLbl);
    osAtt = std::make_unique<APVTS::ComboBoxAttachment> (processor.getAPVTS(), "oversample", oversampleBox);

    // Preset browser.
    addAndMakeVisible (presetBox);
    refreshPresetBox();
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0) processor.getPresetManager().loadPreset (id - 1);
    };
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);
    prevPreset.onClick = [this]
    {
        processor.getPresetManager().loadPrevious();
        presetBox.setSelectedId (processor.getPresetManager().getCurrentIndex() + 1,
                                 juce::dontSendNotification);
    };
    nextPreset.onClick = [this]
    {
        processor.getPresetManager().loadNext();
        presetBox.setSelectedId (processor.getPresetManager().getCurrentIndex() + 1,
                                 juce::dontSendNotification);
    };

    addAndMakeVisible (analyzer);

    // Module panels inside a scrollable viewport.
    for (int i = 0; i < chaos::kNumModules; ++i)
        panels.add (new ModulePanel (processor, i));
    for (auto* pnl : panels) moduleContainer.addAndMakeVisible (pnl);
    viewport.setViewedComponent (&moduleContainer, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setResizable (true, true);
    setResizeLimits (820, 560, 1600, 1200);
    setSize (1040, 720);
}

ChaosRealmAudioProcessorEditor::~ChaosRealmAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ChaosRealmAudioProcessorEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    auto& pm = processor.getPresetManager();
    juce::String lastCat;
    for (int i = 0; i < pm.getNumPresets(); ++i)
    {
        const auto cat = pm.getCategory (i);
        if (cat != lastCat) { presetBox.addSectionHeading (cat); lastCat = cat; }
        presetBox.addItem (pm.getName (i), i + 1);
    }
    presetBox.setTextWhenNothingSelected ("Presets");
    if (pm.getNumPresets() > 0)
        presetBox.setSelectedId (pm.getCurrentIndex() + 1, juce::dontSendNotification);
}

void ChaosRealmAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::background);

    auto header = getLocalBounds().removeFromTop (58);
    juce::ColourGradient grad (C::accent2.withAlpha (0.25f), header.getX(), header.getY(),
                               C::background, header.getX(), header.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (header);

    g.setColour (C::text);
    g.setFont (juce::Font (26.0f, juce::Font::bold));
    g.drawText ("CHAOS REALM", header.reduced (18, 0).removeFromLeft (300),
                juce::Justification::centredLeft);
    g.setColour (C::textDim);
    g.setFont (11.0f);
    g.drawText ("ULTIMATE UNCONVENTIONAL AUDIO PROCESSOR",
                header.reduced (20, 4).removeFromLeft (360).removeFromBottom (16),
                juce::Justification::bottomLeft);
}

void ChaosRealmAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (58);

    // Global controls on the right of the header.
    auto globals = header.removeFromRight (360).reduced (6, 6);
    auto placeG = [&] (juce::Slider& s, juce::Label& l)
    {
        auto cell = globals.removeFromLeft (72);
        l.setBounds (cell.removeFromTop (14));
        s.setBounds (cell);
    };
    placeG (inGain, inGainLbl);
    placeG (outGain, outGainLbl);
    placeG (masterMix, masterMixLbl);
    auto osCell = globals.removeFromLeft (120).reduced (2);
    oversampleLbl.setBounds (osCell.removeFromTop (14));
    oversampleBox.setBounds (osCell.removeFromTop (26));

    // Preset browser in the header centre.
    auto presetArea = header.reduced (12, 14);
    presetArea.removeFromLeft (300); // leave room for the title
    prevPreset.setBounds (presetArea.removeFromLeft (28));
    presetArea.removeFromLeft (4);
    nextPreset.setBounds (presetArea.removeFromRight (28));
    presetArea.removeFromRight (4);
    presetBox.setBounds (presetArea.removeFromLeft (juce::jmin (280, presetArea.getWidth())));

    analyzer.setBounds (b.removeFromTop (150).reduced (8, 4));

    viewport.setBounds (b.reduced (6, 2));
    const int w = viewport.getWidth() - 14;
    int y = 0;
    for (auto* pnl : panels) { pnl->setBounds (0, y, w, pnl->preferredHeight()); y += pnl->preferredHeight(); }
    moduleContainer.setSize (w, y);
}
