#pragma once
#include "PluginProcessor.h"

class MaidFrogEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MaidFrogEditor (MaidFrogProcessor&);
    ~MaidFrogEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Pill;
    class Knob;
    class Library;
    void openLibrary();
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::Rectangle<int> frogArea() const { return { getWidth() - 132, 6, 108, 76 }; }
    std::unique_ptr<Library> library;
    void timerCallback() override;
    void toggleParam (const char* id);
    bool getBool (const char* id) const { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }

    MaidFrogProcessor& proc;
    std::unique_ptr<Pill> learnBtn, clearBtn, adaptBtn, deltaBtn, powerBtn;
    std::unique_ptr<Knob> reduction, sensitivity, air, body, lowcut, room, smooth;
    juce::Font wordmark;
    juce::Image frog;
    juce::Rectangle<int> modView, modCtl;
    float smoothIn[gf::Denoiser::kDisplay] = {}, smoothNz[gf::Denoiser::kDisplay] = {}, smoothG[gf::Denoiser::kDisplay] = {};
};
