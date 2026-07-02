/*
    CHAOS REALM — PresetFactory.h
    Deterministic, framework-free factory-preset generator.

    Rather than hand-author hundreds of XML files, CHAOS REALM synthesises its
    factory library procedurally from a fixed seed, so the bank is large,
    reproducible, and always in sync with the current parameter set.  Each
    preset is a category + name + a list of (paramPath, value) pairs using the
    exact same id scheme as the plugin's APVTS ("m<i>_on", "m<i>_mix",
    "m<i>_p<p>", plus the globals), so the JUCE layer can apply them directly.

    The companion tool tools/generate_presets.cpp serialises the output to
    presets/FactoryPresets.xml, which the plugin embeds and loads at runtime.
*/
#pragma once

#include "ChaosEngine.h"

#include <string>
#include <utility>
#include <vector>

namespace chaos
{

struct Preset
{
    std::string category;
    std::string name;
    std::vector<std::pair<std::string, float>> values; // paramPath -> value
};

class PresetFactory
{
public:
    /** Generate at least 'target' presets describing the given engine. */
    static std::vector<Preset> generate (const ChaosEngine& engine, int target = 512)
    {
        std::vector<Preset> out;
        Xorshift rng (0x0CEA0F17u);

        // --- Category definitions: the module(s) each category features. ---
        struct Category { const char* name; std::vector<int> featured; float intensity; };
        const std::vector<Category> cats = {
            { "Spectral Morph",    { 0 },             0.55f },
            { "Chaos Engines",     { 1 },             0.65f },
            { "Mind Games",        { 2 },             0.50f },
            { "Micro Textures",    { 3 },             0.60f },
            { "Impossible Spaces", { 4 },             0.60f },
            { "Creatures",         { 5 },             0.60f },
            { "Electromagnetic",   { 6 },             0.55f },
            { "Time Smear",        { 7 },             0.60f },
            { "Quantum States",    { 8 },             0.55f },
            { "Occult",            { 9 },             0.55f },
            { "Hybrids",           { 0,3,4,7 },       0.55f },
            { "Rituals",           { 9,4,2 },         0.60f },
            { "Total Chaos",       { 1,3,6,7,8,9 },   0.75f },
        };

        const std::vector<std::string> adjectives = {
            "Liminal","Fractured","Obsidian","Ghostly","Cryogenic","Molten","Hollow",
            "Woven","Spectral","Serrated","Velvet","Corroded","Prismatic","Abyssal",
            "Radiant","Withered","Static","Nebular","Glassine","Feral","Sublime",
            "Distant","Occulted","Iridescent","Vaporous","Granular","Warped","Sacred",
            "Quantum","Aberrant","Luminous","Tectonic","Ashen","Ethereal","Voltaic" };
        const std::vector<std::string> nouns = {
            "Drift","Cathedral","Membrane","Engine","Bloom","Cascade","Filament",
            "Resonance","Halo","Vortex","Lattice","Specter","Circuit","Choir",
            "Mirror","Furnace","Whisper","Continuum","Totem","Sigil","Aurora",
            "Machine","Tide","Fracture","Oracle","Swarm","Monolith","Séance",
            "Field","Pulse","Chamber","Vessel","Relic","Ember","Signal" };

        // A curated "Init" preset (everything off, unity).
        {
            Preset init; init.category = "Init"; init.name = "Init / Clean";
            addGlobals (init, 0.0f, 0.0f, 1.0f);
            for (int i = 0; i < kNumModules; ++i) addModuleOff (init, engine, i);
            out.push_back (init);
        }

        const int perCat = std::max (1, (target - 1) / (int) cats.size() + 1);

        for (const auto& cat : cats)
        {
            for (int n = 0; n < perCat; ++n)
            {
                Preset p;
                p.category = cat.name;
                p.name = adjectives[rng.nextUInt() % adjectives.size()] + " "
                       + nouns[rng.nextUInt() % nouns.size()];

                addGlobals (p, biasedDb (rng, 0.0f, 2.0f), biasedDb (rng, 0.0f, 2.0f),
                            0.75f + 0.25f * rng.nextFloat());

                for (int i = 0; i < kNumModules; ++i) addModuleOff (p, engine, i);

                // Enable + shape the featured modules.
                for (int m : cat.featured)
                {
                    // Occasionally skip a featured module for variety (except the first).
                    if (m != cat.featured.front() && rng.nextFloat() < 0.3f) continue;
                    enableAndRandomise (p, engine, m, cat.intensity, rng);
                }
                // Sometimes fold in one extra random module for surprise.
                if (rng.nextFloat() < 0.35f)
                {
                    const int extra = (int) (rng.nextUInt() % (uint32_t) kNumModules);
                    enableAndRandomise (p, engine, extra, cat.intensity * 0.6f, rng);
                }

                out.push_back (p);
                if ((int) out.size() >= target) break;
            }
            if ((int) out.size() >= target) break;
        }

        return out;
    }

private:
    static std::string tag (int i) { return "m" + std::to_string (i); }

    static void addGlobals (Preset& p, float inDb, float outDb, float masterMix)
    {
        p.values.push_back ({ "in_gain",   inDb });
        p.values.push_back ({ "out_gain",  outDb });
        p.values.push_back ({ "master_mix", masterMix });
    }

    static void addModuleOff (Preset& p, const ChaosEngine& engine, int i)
    {
        p.values.push_back ({ tag (i) + "_on", 0.0f });
        p.values.push_back ({ tag (i) + "_mix", 1.0f });
        const auto& m = engine.module (i);
        for (int q = 0; q < m.getNumParameters(); ++q)
            p.values.push_back ({ tag (i) + "_p" + std::to_string (q),
                                  m.getParameterInfo (q).defaultValue });
    }

    static void enableAndRandomise (Preset& p, const ChaosEngine& engine, int i,
                                    float intensity, Xorshift& rng)
    {
        // Overwrite the previously-added "off" entries for this module.
        setValue (p, tag (i) + "_on", 1.0f);
        setValue (p, tag (i) + "_mix", 0.4f + 0.6f * intensity * (0.6f + 0.4f * rng.nextFloat()));

        const auto& m = engine.module (i);
        for (int q = 0; q < m.getNumParameters(); ++q)
        {
            const auto& info = m.getParameterInfo (q);
            float v;
            if (info.isStepped && info.numSteps > 1)
            {
                // Pick a discrete step.
                const int step = (int) (rng.nextUInt() % (uint32_t) info.numSteps);
                v = (float) step / (float) (info.numSteps - 1);
            }
            else
            {
                // Blend the default with a random value, weighted by intensity.
                const float r = rng.nextFloat();
                v = clampf (info.defaultValue * (1.0f - intensity) + r * intensity
                            + 0.15f * (rng.nextFloat() - 0.5f), 0.0f, 1.0f);
            }
            setValue (p, tag (i) + "_p" + std::to_string (q), v);
        }
    }

    static void setValue (Preset& p, const std::string& id, float v)
    {
        for (auto& kv : p.values) if (kv.first == id) { kv.second = v; return; }
        p.values.push_back ({ id, v });
    }

    static float biasedDb (Xorshift& rng, float centre, float spread)
    {
        return centre + (rng.nextFloat() - 0.5f) * 2.0f * spread;
    }
};

} // namespace chaos
