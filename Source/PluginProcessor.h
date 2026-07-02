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
#include "dsp/PresetFactory.h"
#include "PresetManager.h"

#include <array>
#include <vector>

//==============================================================================
/** Also a ChangeBroadcaster: it notifies an open editor after a host state
    load so the editor can re-sync its panel order / A-B / preset display. */
class ChaosRealmAudioProcessor : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster
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

    // -- Chain routing (persisted in state, applied each block) ------------
    /** order[position] = module index. Sanitised to a valid permutation. */
    void setChainOrder (const std::array<int, chaos::kNumModules>& order);
    std::array<int, chaos::kNumModules> getChainOrder() const;

    // -- A/B compare -------------------------------------------------------
    void setActiveABSlot (int slot);            // 0 = A, 1 = B
    int  getActiveABSlot() const noexcept { return abActive; }
    void copyActiveABToOther();                 // duplicate active slot into the other

    // -- Randomize ---------------------------------------------------------
    void randomize (float amount);              // amount 0..1 (wildness)

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
    void writeChainOrderToState();      // atomics -> state property
    void applyChainOrderFromState();    // state property -> atomics
    void applyPresetValues (const chaos::Preset& preset);

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

    // Chain routing: the permutation is packed into a single atomic (4 bits per
    // module index, 10 modules = 40 bits) so the audio thread always reads a
    // consistent, non-torn order. The state-tree property mirrors it for
    // persistence.
    std::atomic<uint64_t> chainOrderPacked { 0 };
    static uint64_t packOrder (const std::array<int, chaos::kNumModules>& o) noexcept
    {
        uint64_t v = 0;
        for (int i = 0; i < chaos::kNumModules; ++i)
            v |= (uint64_t) (o[(size_t) i] & 0xF) << (i * 4);
        return v;
    }
    static std::array<int, chaos::kNumModules> unpackOrder (uint64_t v) noexcept
    {
        std::array<int, chaos::kNumModules> o {};
        for (int i = 0; i < chaos::kNumModules; ++i)
            o[(size_t) i] = (int) ((v >> (i * 4)) & 0xF);
        return o;
    }

    // A/B compare snapshots (message-thread only).
    juce::ValueTree abState[2];
    int abActive = 0;

    uint32_t randSeed = 0x51ED51EDu; // advanced on each randomize()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChaosRealmAudioProcessor)
};
