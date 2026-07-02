/*
    CHAOS REALM — Ultimate Unconventional Audio Processor
    PluginProcessor.h — JUCE AudioProcessor bridging the host to the pure-C++
    ChaosEngine DSP core.

    This is the ONLY file (with PluginEditor) that depends on JUCE.  All actual
    audio processing lives in the framework-free dsp/ layer.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/ChaosEngine.h"
#include "PresetManager.h"

#include <array>
#include <vector>

//==============================================================================
class ChaosRealmAudioProcessor : public juce::AudioProcessor
{
public:
    ChaosRealmAudioProcessor();
    ~ChaosRealmAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    chaos::ChaosEngine&                 getEngine() noexcept { return engine; }
    PresetManager&                      getPresetManager() noexcept { return presetManager; }

    /** Flat list of every modulation destination (module param), built once. */
    struct Destination { int moduleIndex, paramIndex; juce::String label; };
    const std::vector<Destination>& destinations() const noexcept { return destList; }

    /** Oversampling factor as a power of two (0..4 -> 1x..16x). */
    static constexpr int kMaxOversampleOrder = 4;

    /** Lock-free scope for the editor's visualizer.  Copies the most recent
        'num' mono-summed output samples into dest (single-producer/consumer;
        benign tearing only). */
    void copyScope (float* dest, int num) const noexcept;
    static constexpr int kScopeCapacity = 1 << 14; // 16384

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void buildDestinationList();
    void pushParametersToEngine();
    void configureModulation();

    //==========================================================================
    chaos::ChaosEngine engine;                 // declared BEFORE apvts (used by createLayout)
    std::vector<Destination> destList;
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { apvts };

    // Cached raw parameter pointers for lock-free per-block access.
    std::atomic<float>* pInGain  = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    std::atomic<float>* pMasterMix = nullptr;
    std::atomic<float>* pOversample = nullptr;

    struct ModuleParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* mix = nullptr;
        std::vector<std::atomic<float>*> params;
    };
    std::array<ModuleParams, chaos::kNumModules> moduleParams;

    static constexpr int kNumMacros = 4;
    static constexpr int kNumLfos   = 4;
    static constexpr int kNumModSlots = 6;
    std::array<std::atomic<float>*, kNumMacros> pMacros { };
    struct LfoParams { std::atomic<float>* rate = nullptr; std::atomic<float>* shape = nullptr; };
    std::array<LfoParams, kNumLfos> pLfos;
    struct ModSlotParams { std::atomic<float>* src = nullptr; std::atomic<float>* dst = nullptr; std::atomic<float>* depth = nullptr; };
    std::array<ModSlotParams, kNumModSlots> pModSlots;

    // Oversampling (JUCE) — variable 1x..16x.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentOversampleOrder = 0;
    double baseSampleRate = 44100.0;
    int    baseBlockSize  = 512;

    // Lock-free scope ring (single producer = audio thread, single consumer = UI).
    std::vector<float> scopeRing;
    std::atomic<int>   scopeWritePos { 0 };
    void pushToScope (const juce::AudioBuffer<float>& buffer, int numCh) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChaosRealmAudioProcessor)
};
