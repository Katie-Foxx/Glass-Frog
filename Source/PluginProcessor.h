#pragma once
// Glass Frog: noise reduction for voice. It takes the noise floor of the microphone and of the room away without darkening the top of the voice.
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Denoise.h"

namespace gfid
{
inline constexpr const char* reduction = "reduction";
inline constexpr const char* sensitivity = "sensitivity";
inline constexpr const char* air = "air";
inline constexpr const char* body = "body";
inline constexpr const char* lowcut = "lowcut";
inline constexpr const char* room = "room";
inline constexpr const char* smooth = "smooth";
inline constexpr const char* adaptive = "adaptive";
inline constexpr const char* delta = "delta";
inline constexpr const char* power = "power";
}

class GlassFrogProcessor : public juce::AudioProcessor
{
public:
    GlassFrogProcessor();
    ~GlassFrogProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto o = l.getMainOutputChannelSet();
        return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && o == l.getMainInputChannelSet();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Glass Frog B"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    // the editor asks, the audio thread does
    void requestLearn (bool on) { cmd.store (on ? 1 : 2); }
    void requestClear() { cmd.store (3); }
    bool isLearning() const { return learningFlag.load(); }
    bool hasProfile() const { return hasProfileFlag.load(); }

    juce::AudioProcessorValueTreeState apvts;
    gf::Denoiser dn;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    std::atomic<int> cmd { 0 };
    std::atomic<bool> learningFlag { false }, hasProfileFlag { false };
    std::vector<float> storedProfile;
    double sr = 48000.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlassFrogProcessor)
};
