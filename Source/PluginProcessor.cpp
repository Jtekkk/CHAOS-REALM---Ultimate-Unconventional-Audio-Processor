/*
    CHAOS REALM — PluginProcessor.cpp
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
juce::String moduleTag (int i) { return "m" + juce::String (i); }

const juce::StringArray kSourceChoices {
    "None", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Envelope", "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
const juce::StringArray kLfoShapeChoices {
    "Sine", "Triangle", "Saw", "Square", "Sample & Hold", "Chaos" };
const juce::StringArray kOversampleChoices { "1x", "2x", "4x", "8x", "16x" };
}

//==============================================================================
ChaosRealmAudioProcessor::ChaosRealmAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      engine(),                         // constructed first — createLayout enumerates its params
      apvts (*this, nullptr, "CHAOSREALM", createLayout())
{
    // Cache raw pointers.
    pInGain     = apvts.getRawParameterValue ("in_gain");
    pOutGain    = apvts.getRawParameterValue ("out_gain");
    pMasterMix  = apvts.getRawParameterValue ("master_mix");
    pOversample = apvts.getRawParameterValue ("oversample");

    for (int i = 0; i < chaos::kNumModules; ++i)
    {
        auto& mp = moduleParams[(size_t) i];
        mp.on  = apvts.getRawParameterValue (moduleTag (i) + "_on");
        mp.mix = apvts.getRawParameterValue (moduleTag (i) + "_mix");
        const int np = engine.module (i).getNumParameters();
        mp.params.resize ((size_t) np);
        for (int p = 0; p < np; ++p)
            mp.params[(size_t) p] = apvts.getRawParameterValue (moduleTag (i) + "_p" + juce::String (p));
    }

    for (int k = 0; k < kNumMacros; ++k)
        pMacros[(size_t) k] = apvts.getRawParameterValue ("macro" + juce::String (k + 1));
    for (int k = 0; k < kNumLfos; ++k)
    {
        pLfos[(size_t) k].rate  = apvts.getRawParameterValue ("lfo" + juce::String (k + 1) + "_rate");
        pLfos[(size_t) k].shape = apvts.getRawParameterValue ("lfo" + juce::String (k + 1) + "_shape");
    }
    for (int s = 0; s < kNumModSlots; ++s)
    {
        pModSlots[(size_t) s].src   = apvts.getRawParameterValue ("mod" + juce::String (s + 1) + "_src");
        pModSlots[(size_t) s].dst   = apvts.getRawParameterValue ("mod" + juce::String (s + 1) + "_dst");
        pModSlots[(size_t) s].depth = apvts.getRawParameterValue ("mod" + juce::String (s + 1) + "_depth");
    }

    // Default identity chain order, mirrored into the state tree.
    for (int i = 0; i < chaos::kNumModules; ++i) chainOrder[(size_t) i].store (i);
    writeChainOrderToState();

    // Initialise both A/B slots from the current (default) state.
    abState[0] = apvts.copyState();
    abState[1] = apvts.copyState();
}

//==============================================================================
void ChaosRealmAudioProcessor::buildDestinationList()
{
    destList.clear();
    for (int i = 0; i < chaos::kNumModules; ++i)
    {
        auto& m = engine.module (i);
        for (int p = 0; p < m.getNumParameters(); ++p)
            destList.push_back ({ i, p, juce::String (m.getName()) + " : "
                                        + juce::String (m.getParameterInfo (p).name.c_str()) });
    }
}

APVTS::ParameterLayout ChaosRealmAudioProcessor::createLayout()
{
    // Populate the destination list FIRST — it is consumed below when building
    // the modulation-slot destination choices.  (createLayout runs during the
    // apvts member initialisation, before the constructor body, so we cannot
    // rely on a later buildDestinationList() call.)
    buildDestinationList();

    APVTS::ParameterLayout layout;
    using FloatP  = juce::AudioParameterFloat;
    using BoolP   = juce::AudioParameterBool;
    using ChoiceP = juce::AudioParameterChoice;
    using PID     = juce::ParameterID;
    constexpr int ver = 1;

    // ---- Global ----
    layout.add (std::make_unique<FloatP> (PID { "in_gain",  ver }, "Input Gain",
                 juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<FloatP> (PID { "out_gain", ver }, "Output Gain",
                 juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<FloatP> (PID { "master_mix", ver }, "Master Mix",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<ChoiceP> (PID { "oversample", ver }, "Oversampling",
                 kOversampleChoices, 0));

    // ---- Per module ----
    for (int i = 0; i < chaos::kNumModules; ++i)
    {
        auto& m = engine.module (i);
        const juce::String tag  = moduleTag (i);
        const juce::String name = juce::String (m.getName());

        layout.add (std::make_unique<BoolP> (PID { tag + "_on", ver },
                     name + " On", engine.isModuleEnabled (i)));
        layout.add (std::make_unique<FloatP> (PID { tag + "_mix", ver }, name + " Mix",
                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

        for (int p = 0; p < m.getNumParameters(); ++p)
        {
            const auto info = m.getParameterInfo (p);
            const float minD = info.minDisplay, maxD = info.maxDisplay;
            const juce::String unit = juce::String (info.unit.c_str());
            auto stringFromValue = [minD, maxD, unit] (float v01, int) -> juce::String
            {
                const float disp = minD + (maxD - minD) * v01;
                return juce::String (disp, disp >= 100.0f ? 0 : 2) + (unit.isEmpty() ? "" : " " + unit);
            };
            layout.add (std::make_unique<FloatP> (PID { tag + "_p" + juce::String (p), ver },
                         name.substring (0, 4) + " " + juce::String (info.name.c_str()),
                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), info.defaultValue,
                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (stringFromValue)));
        }
    }

    // ---- Modulation ----
    for (int k = 0; k < kNumMacros; ++k)
        layout.add (std::make_unique<FloatP> (PID { "macro" + juce::String (k + 1), ver },
                     "Macro " + juce::String (k + 1),
                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    for (int k = 0; k < kNumLfos; ++k)
    {
        layout.add (std::make_unique<FloatP> (PID { "lfo" + juce::String (k + 1) + "_rate", ver },
                     "LFO " + juce::String (k + 1) + " Rate",
                     juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 1.0f,
                     juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<ChoiceP> (PID { "lfo" + juce::String (k + 1) + "_shape", ver },
                     "LFO " + juce::String (k + 1) + " Shape", kLfoShapeChoices, 0));
    }
    for (int s = 0; s < kNumModSlots; ++s)
    {
        layout.add (std::make_unique<ChoiceP> (PID { "mod" + juce::String (s + 1) + "_src", ver },
                     "Mod " + juce::String (s + 1) + " Source", kSourceChoices, 0));
        // Destination choices: "None" + one entry per destination.
        juce::StringArray destChoices; destChoices.add ("None");
        for (auto& d : destList) destChoices.add (d.label);
        layout.add (std::make_unique<ChoiceP> (PID { "mod" + juce::String (s + 1) + "_dst", ver },
                     "Mod " + juce::String (s + 1) + " Dest", destChoices, 0));
        layout.add (std::make_unique<FloatP> (PID { "mod" + juce::String (s + 1) + "_depth", ver },
                     "Mod " + juce::String (s + 1) + " Depth",
                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    }

    return layout;
}

//==============================================================================
bool ChaosRealmAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ChaosRealmAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;
    baseBlockSize  = samplesPerBlock;

    currentOversampleOrder = (int) (pOversample ? pOversample->load() : 0);
    currentOversampleOrder = juce::jlimit (0, kMaxOversampleOrder, currentOversampleOrder);

    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, (size_t) currentOversampleOrder,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampler->initProcessing ((size_t) samplesPerBlock);

    const double osSr = sampleRate * (double) (1 << currentOversampleOrder);
    const int    osBlock = samplesPerBlock * (1 << currentOversampleOrder);
    engine.prepare (osSr, osBlock, numCh);

    scopeRing.assign ((size_t) kScopeCapacity, 0.0f);
    scopeWritePos.store (0);

    const int factor = 1 << currentOversampleOrder;
    const int engLatencyBase = (engine.getLatencySamples() + factor - 1) / factor;
    setLatencySamples (engLatencyBase + (int) oversampler->getLatencyInSamples());
}

//==============================================================================
void ChaosRealmAudioProcessor::pushParametersToEngine()
{
    // Apply the (lock-free) chain routing.
    std::array<int, chaos::kNumModules> order {};
    for (int i = 0; i < chaos::kNumModules; ++i) order[(size_t) i] = chainOrder[(size_t) i].load();
    engine.setRouting (order);

    engine.setInputGainDb  (pInGain  ? pInGain->load()  : 0.0f);
    engine.setOutputGainDb (pOutGain ? pOutGain->load() : 0.0f);
    engine.setMasterMix    (pMasterMix ? pMasterMix->load() : 1.0f);

    for (int i = 0; i < chaos::kNumModules; ++i)
    {
        auto& mp = moduleParams[(size_t) i];
        engine.setModuleEnabled (i, mp.on && mp.on->load() > 0.5f);
        engine.setModuleMix (i, mp.mix ? mp.mix->load() : 1.0f);
        auto& m = engine.module (i);
        for (size_t p = 0; p < mp.params.size(); ++p)
            if (mp.params[p]) m.setParameter ((int) p, mp.params[p]->load());
    }
}

void ChaosRealmAudioProcessor::configureModulation()
{
    auto& mm = engine.modMatrix();
    for (int k = 0; k < kNumLfos; ++k)
    {
        auto& lfo = mm.lfo (k);
        if (pLfos[(size_t) k].rate)  lfo.setRateHz (pLfos[(size_t) k].rate->load());
        if (pLfos[(size_t) k].shape)
            lfo.setShape (static_cast<chaos::LFO::Shape> (
                juce::jlimit (0, (int) chaos::LFO::Shape::NumShapes - 1,
                              (int) pLfos[(size_t) k].shape->load())));
    }
    for (int k = 0; k < kNumMacros; ++k)
        if (pMacros[(size_t) k]) mm.setMacro (k, pMacros[(size_t) k]->load());

    for (int s = 0; s < kNumModSlots; ++s)
    {
        auto& slot = mm.slot (s);
        const int srcIdx = pModSlots[(size_t) s].src ? (int) pModSlots[(size_t) s].src->load() : 0;
        const int dstIdx = pModSlots[(size_t) s].dst ? (int) pModSlots[(size_t) s].dst->load() : 0;
        slot.source = static_cast<chaos::ModMatrix::Source> (
            juce::jlimit (0, (int) kSourceChoices.size() - 1, srcIdx));
        slot.depth = pModSlots[(size_t) s].depth ? pModSlots[(size_t) s].depth->load() : 0.0f;
        if (dstIdx > 0 && dstIdx <= (int) destList.size())
        {
            slot.destModule = destList[(size_t) (dstIdx - 1)].moduleIndex;
            slot.destParam  = destList[(size_t) (dstIdx - 1)].paramIndex;
        }
        else { slot.destModule = -1; slot.destParam = -1; }
    }
}

void ChaosRealmAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();
    for (int c = totalIn; c < totalOut; ++c) buffer.clear (c, 0, buffer.getNumSamples());

    // Handle a live oversampling change.
    const int desired = juce::jlimit (0, kMaxOversampleOrder,
                                      pOversample ? (int) pOversample->load() : 0);
    if (desired != currentOversampleOrder)
        prepareToPlay (baseSampleRate, baseBlockSize);

    pushParametersToEngine();
    configureModulation();

    const int numCh = juce::jmax (1, totalOut);
    std::array<float*, 8> ptrs { };

    if (currentOversampleOrder == 0)
    {
        for (int c = 0; c < numCh; ++c) ptrs[(size_t) c] = buffer.getWritePointer (c);
        engine.process (ptrs.data(), numCh, buffer.getNumSamples());
    }
    else
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);
        for (int c = 0; c < numCh; ++c) ptrs[(size_t) c] = up.getChannelPointer ((size_t) c);
        engine.process (ptrs.data(), numCh, (int) up.getNumSamples());
        oversampler->processSamplesDown (block);
    }

    pushToScope (buffer, numCh);
}

void ChaosRealmAudioProcessor::pushToScope (const juce::AudioBuffer<float>& buffer, int numCh) noexcept
{
    if (scopeRing.empty()) return;
    const int n = buffer.getNumSamples();
    int w = scopeWritePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < numCh; ++c) s += buffer.getReadPointer (c)[i];
        scopeRing[(size_t) w] = s / (float) juce::jmax (1, numCh);
        w = (w + 1) & (kScopeCapacity - 1);
    }
    scopeWritePos.store (w, std::memory_order_release);
}

void ChaosRealmAudioProcessor::copyScope (float* dest, int num) const noexcept
{
    if (scopeRing.empty()) { for (int i = 0; i < num; ++i) dest[i] = 0.0f; return; }
    const int w = scopeWritePos.load (std::memory_order_acquire);
    int r = (w - num) & (kScopeCapacity - 1);
    for (int i = 0; i < num; ++i) { dest[i] = scopeRing[(size_t) r]; r = (r + 1) & (kScopeCapacity - 1); }
}

//==============================================================================
juce::AudioProcessorEditor* ChaosRealmAudioProcessor::createEditor()
{
    return new ChaosRealmAudioProcessorEditor (*this);
}

void ChaosRealmAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    writeChainOrderToState(); // ensure the persisted order is current
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChaosRealmAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            applyChainOrderFromState();
            abState[(size_t) abActive] = apvts.copyState();
        }
}

//==============================================================================
//  Chain routing
//==============================================================================
void ChaosRealmAudioProcessor::writeChainOrderToState()
{
    juce::String s;
    for (int i = 0; i < chaos::kNumModules; ++i)
        s << chainOrder[(size_t) i].load() << (i + 1 < chaos::kNumModules ? "," : "");
    apvts.state.setProperty ("chainOrder", s, nullptr);
}

void ChaosRealmAudioProcessor::applyChainOrderFromState()
{
    const juce::String s = apvts.state.getProperty ("chainOrder").toString();
    if (s.isEmpty()) return;

    auto tokens = juce::StringArray::fromTokens (s, ",", "");
    std::array<int, chaos::kNumModules> order {};
    bool seen[chaos::kNumModules] = { false };
    int count = 0;
    for (int i = 0; i < tokens.size() && count < chaos::kNumModules; ++i)
    {
        const int v = tokens[i].getIntValue();
        if (v >= 0 && v < chaos::kNumModules && ! seen[v]) { order[(size_t) count++] = v; seen[v] = true; }
    }
    // Fill any missing indices to guarantee a valid permutation.
    for (int v = 0; v < chaos::kNumModules && count < chaos::kNumModules; ++v)
        if (! seen[v]) order[(size_t) count++] = v;

    for (int i = 0; i < chaos::kNumModules; ++i) chainOrder[(size_t) i].store (order[(size_t) i]);
}

void ChaosRealmAudioProcessor::setChainOrder (const std::array<int, chaos::kNumModules>& order)
{
    // Sanitise to a valid permutation.
    std::array<int, chaos::kNumModules> clean {};
    bool seen[chaos::kNumModules] = { false };
    int count = 0;
    for (int i = 0; i < chaos::kNumModules; ++i)
    {
        const int v = order[(size_t) i];
        if (v >= 0 && v < chaos::kNumModules && ! seen[v]) { clean[(size_t) count++] = v; seen[v] = true; }
    }
    for (int v = 0; v < chaos::kNumModules && count < chaos::kNumModules; ++v)
        if (! seen[v]) clean[(size_t) count++] = v;

    for (int i = 0; i < chaos::kNumModules; ++i) chainOrder[(size_t) i].store (clean[(size_t) i]);
    writeChainOrderToState();
}

std::array<int, chaos::kNumModules> ChaosRealmAudioProcessor::getChainOrder() const
{
    std::array<int, chaos::kNumModules> order {};
    for (int i = 0; i < chaos::kNumModules; ++i) order[(size_t) i] = chainOrder[(size_t) i].load();
    return order;
}

//==============================================================================
//  A/B compare
//==============================================================================
void ChaosRealmAudioProcessor::setActiveABSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abActive) return;
    abState[(size_t) abActive] = apvts.copyState();      // save current into the active slot
    abActive = slot;
    if (abState[(size_t) slot].isValid())
    {
        apvts.replaceState (abState[(size_t) slot].createCopy());
        applyChainOrderFromState();
    }
}

void ChaosRealmAudioProcessor::copyActiveABToOther()
{
    abState[(size_t) abActive] = apvts.copyState();
    abState[(size_t) (abActive ^ 1)] = apvts.copyState();
}

//==============================================================================
//  Randomize
//==============================================================================
void ChaosRealmAudioProcessor::applyPresetValues (const chaos::Preset& preset)
{
    for (const auto& kv : preset.values)
        if (auto* param = apvts.getParameter (juce::String (kv.first)))
            param->setValueNotifyingHost (param->convertTo0to1 (kv.second));
}

void ChaosRealmAudioProcessor::randomize (float amount)
{
    randSeed = randSeed * 1664525u + 1013904223u;        // advance the LCG
    const auto preset = chaos::PresetFactory::randomPreset (engine, randSeed, amount);
    applyPresetValues (preset);
    // Random presets don't touch routing; keep the current chain order.
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChaosRealmAudioProcessor();
}
