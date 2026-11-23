#pragma once
#include "PluginProcessor.h"

class GlassFrogEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlassFrogEditor (GlassFrogProcessor&);
    ~GlassFrogEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Pill;
    class Knob;
    void timerCallback() override;
    void toggleParam (const char* id);
    bool getBool (const char* id) const { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }

    GlassFrogProcessor& proc;
    std::unique_ptr<Pill> learnBtn, clearBtn, adaptBtn, deltaBtn, powerBtn;
    std::unique_ptr<Knob> reduction, sensitivity, air, body, lowcut, room, smooth;
    juce::Font wordmark;
    juce::Image frog;
    juce::Rectangle<int> modView, modCtl;
    float smoothIn[gf::Denoiser::kDisplay] = {}, smoothNz[gf::Denoiser::kDisplay] = {}, smoothG[gf::Denoiser::kDisplay] = {};
};
