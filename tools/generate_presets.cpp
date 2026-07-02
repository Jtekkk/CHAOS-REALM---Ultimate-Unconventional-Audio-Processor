/*
    CHAOS REALM — tools/generate_presets.cpp
    Standalone (JUCE-free) generator that writes the factory preset bank to
    presets/FactoryPresets.xml.  Run from the repo root:

        g++ -std=c++17 -O2 tools/generate_presets.cpp Source/dsp/ChaosEngine.cpp \
            Source/dsp/modules/*.cpp -I. -o /tmp/genpresets
        /tmp/genpresets

    The plugin embeds the produced XML and loads it at runtime.
*/
#include "Source/dsp/ChaosEngine.h"
#include "Source/dsp/modules/AllModules.h"
#include "Source/dsp/PresetFactory.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace chaos;

static std::string xmlEscape (const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        switch (c)
        {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"':  o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default:   o += c;        break;
        }
    }
    return o;
}

int main (int argc, char** argv)
{
    const int target = argc > 1 ? std::atoi (argv[1]) : 512;
    const char* outPath = argc > 2 ? argv[2] : "presets/FactoryPresets.xml";

    ChaosEngine engine; // constructs all modules -> exposes their parameter sets
    const auto presets = PresetFactory::generate (engine, target);

    std::ofstream f (outPath);
    if (! f) { std::fprintf (stderr, "cannot open %s\n", outPath); return 1; }

    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << "<ChaosRealmPresets version=\"1\" count=\"" << presets.size() << "\">\n";
    for (const auto& p : presets)
    {
        f << "  <Preset category=\"" << xmlEscape (p.category)
          << "\" name=\"" << xmlEscape (p.name) << "\">\n";
        for (const auto& kv : p.values)
            f << "    <P id=\"" << kv.first << "\" v=\"" << kv.second << "\"/>\n";
        f << "  </Preset>\n";
    }
    f << "</ChaosRealmPresets>\n";
    f.close();

    std::printf ("Wrote %zu presets to %s\n", presets.size(), outPath);
    return 0;
}
