/*
    CHAOS REALM — PresetManager.h
    Loads the embedded factory preset bank (presets/FactoryPresets.xml, compiled
    in via juce_add_binary_data) and applies presets to the APVTS.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class PresetManager
{
public:
    struct Entry { juce::String category, name; std::unique_ptr<juce::XmlElement> xml; };

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    int         getNumPresets() const noexcept { return (int) presets.size(); }
    juce::String getCategory (int i) const     { return isValid (i) ? presets[(size_t) i].category : juce::String(); }
    juce::String getName (int i) const         { return isValid (i) ? presets[(size_t) i].name     : juce::String(); }
    int         getCurrentIndex() const noexcept { return current; }

    /** Apply preset i to the parameter tree (converts stored values to 0..1). */
    void loadPreset (int i);
    void loadNext()     { if (! presets.empty()) loadPreset ((current + 1) % (int) presets.size()); }
    void loadPrevious() { if (! presets.empty()) loadPreset ((current - 1 + (int) presets.size()) % (int) presets.size()); }

private:
    bool isValid (int i) const noexcept { return i >= 0 && i < (int) presets.size(); }
    void loadFromBinaryData();

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> presets;
    int current = 0;
};
