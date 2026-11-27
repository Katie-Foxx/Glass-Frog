#pragma once
// Maid Frog: noise reduction for voice. It takes the noise floor of the microphone and of the room away without darkening the top of the voice.
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

class MaidFrogProcessor : public juce::AudioProcessor
{
public:
    MaidFrogProcessor();
    ~MaidFrogProcessor() override = default;

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
    const juce::String getName() const override { return "Maid Frog"; }
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
    void requestClear();
    bool isLearning() const { return learningFlag.load(); }
    bool hasProfile() const { return hasProfileFlag.load(); }

    // ---- noise library: named background-noise profiles (SM7B, NT1, ...) kept as files, so every project and instance sees them
    struct NoiseInfo { juce::String name; double gainDb = 0.0; };
    std::vector<NoiseInfo> libraryInfo() const;
    void rescanLibrary();
    int  activeIndex() const;                         // -1 when the profile in use is not saved in the library
    bool hasActive() const;
    juce::String activeName() const;
    double activeGainDb() const;
    void syncLearned();                               // takes a profile that has just been learned as the one in use (message thread)
    bool saveCurrent (const juce::String& name);      // saves the profile in use (with its level) under this name
    void selectEntry (int i);
    void stepEntry (int delta);                       // previous / next in the list
    void setActiveGainDb (double db, bool commit);    // level of the stored noise, -36 .. +36 dB
    void renameEntry (int i, const juce::String& name);
    void deleteEntry (int i);
    void requestMatch() { matchState.store (1); cmd.store (4); }
    int  pollMatch();                                 // 0 nothing / waiting, 2 matched (level already applied), 3 no floor measured yet
    double lastMatchDb() const { return matchDb.load(); }

    juce::AudioProcessorValueTreeState apvts;
    gf::Denoiser dn;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    std::atomic<int> cmd { 0 };
    std::atomic<bool> learningFlag { false }, hasProfileFlag { false };
    struct NoiseEntry { juce::String name; std::vector<float> data; int N = 0; double sr = 0.0; double gainDb = 0.0; };
    static juce::File libraryDir();
    static void writeEntry (const NoiseEntry&);
    static bool readEntry (const juce::File&, NoiseEntry&);
    void applyActive();
    mutable juce::CriticalSection libLock;
    std::vector<NoiseEntry> library;
    NoiseEntry active;                                // the profile in use: stored un-trimmed, the level is applied when it is handed to the denoiser
    bool activeValid = false;
    int  activeIdx = -1;
    std::atomic<bool> learnDone { false };
    std::atomic<int> matchState { 0 };
    std::atomic<float> matchDb { 0.0f };
    double sr = 48000.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaidFrogProcessor)
};
