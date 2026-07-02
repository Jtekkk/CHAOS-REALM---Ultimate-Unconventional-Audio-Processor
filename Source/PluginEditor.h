/*
    CHAOS REALM — PluginEditor.h
    Modular workspace UI: header globals, a live spectrum analyzer, and a
    scrollable stack of ten module panels each exposing enable / mix / params.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "PluginProcessor.h"
#include "gui/ChaosLookAndFeel.h"

//==============================================================================
/** Real-time FFT spectrum + waveform visualizer fed by the processor's scope. */
class SpectrumAnalyzer : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumAnalyzer (ChaosRealmAudioProcessor& p);
    ~SpectrumAnalyzer() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    static constexpr int kFFTOrder = 11;                 // 2048
    static constexpr int kFFTSize  = 1 << kFFTOrder;

    ChaosRealmAudioProcessor& processor;
    juce::dsp::FFT fft { kFFTOrder };
    juce::dsp::WindowingFunction<float> window { kFFTSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> scratch, fftData, magnitudes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};

//==============================================================================
/** One collapsible panel controlling a single CHAOS REALM module. */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (ChaosRealmAudioProcessor& p, int moduleIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    int  preferredHeight() const noexcept;

private:
    ChaosRealmAudioProcessor& processor;
    int index;
    juce::String moduleName;

    juce::ToggleButton enableButton;
    juce::Slider mixSlider;
    juce::Label  mixLabel;
    juce::OwnedArray<juce::Slider> paramSliders;
    juce::OwnedArray<juce::Label>  paramLabels;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> paramAtts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulePanel)
};

//==============================================================================
class ChaosRealmAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChaosRealmAudioProcessorEditor (ChaosRealmAudioProcessor&);
    ~ChaosRealmAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChaosRealmAudioProcessor& processor;
    chaosui::ChaosLookAndFeel lookAndFeel;

    // Header globals.
    juce::Slider inGain, outGain, masterMix;
    juce::Label  inGainLbl, outGainLbl, masterMixLbl;
    juce::ComboBox oversampleBox;
    juce::Label  oversampleLbl;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inAtt, outAtt, mixAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAtt;

    // Preset browser.
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    void refreshPresetBox();

    SpectrumAnalyzer analyzer;

    juce::Viewport viewport;
    juce::Component moduleContainer;
    juce::OwnedArray<ModulePanel> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChaosRealmAudioProcessorEditor)
};
