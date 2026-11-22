#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout GlassFrogProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float def, const char* unit = "")
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.01f), def, AudioParameterFloatAttributes().withLabel (unit)));
    };
    flt (gfid::reduction, "Reduction", 0.0f, 60.0f, 24.0f, " dB");
    flt (gfid::sensitivity, "Sensitivity", 0.0f, 1.0f, 0.5f);
    flt (gfid::air, "Air", 0.0f, 1.0f, 0.6f);
    flt (gfid::room, "Room", 0.0f, 1.0f, 0.0f);
    flt (gfid::smooth, "Smooth", 0.0f, 1.0f, 0.5f);
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { gfid::adaptive, 1 }, "Adaptive", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { gfid::delta, 1 }, "Listen to Removed", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { gfid::power, 1 }, "Power", true));
    return { p.begin(), p.end() };
}

GlassFrogProcessor::GlassFrogProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GlassFrog", createLayout())
{
}

void GlassFrogProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    dn.prepare (sr);
    setLatencySamples (dn.latency());
    if ((int) storedProfile.size() == dn.bins()) dn.setProfile (storedProfile);
    learningFlag = false; hasProfileFlag = (int) storedProfile.size() == dn.bins();
}

void GlassFrogProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), ch = buffer.getNumChannels();
    for (int c = getTotalNumInputChannels(); c < ch; ++c) buffer.clear (c, 0, n);

    switch (cmd.exchange (0))
    {
        case 1: dn.startLearn(); break;
        case 2: dn.stopLearn(); break;
        case 3: dn.stopLearn(); dn.clearProfile(); storedProfile.clear(); break;
        default: break;
    }
    gf::Settings s;
    s.reductionDb = get (gfid::reduction); s.sensitivity = get (gfid::sensitivity); s.air = get (gfid::air);
    s.room = get (gfid::room); s.smooth = get (gfid::smooth);
    s.adaptive = get (gfid::adaptive) > 0.5f; s.delta = get (gfid::delta) > 0.5f; s.enabled = get (gfid::power) > 0.5f;
    dn.setSettings (s);

    auto* l = buffer.getWritePointer (0);
    auto* r = ch > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        double a = l[i], b = r ? (double) r[i] : a;
        dn.processFrame (a, b);
        l[i] = (float) a; if (r) r[i] = (float) b;
    }
    learningFlag = dn.isLearning();
    const bool hp = dn.hasProfile();
    if (hp && ! hasProfileFlag.load()) { auto v = dn.profile(); if (! v.empty()) storedProfile = std::move (v); }
    hasProfileFlag = hp;
}

AudioProcessorEditor* GlassFrogProcessor::createEditor() { return new GlassFrogEditor (*this); }

void GlassFrogProcessor::getStateInformation (MemoryBlock& dest)
{
    auto st = apvts.copyState();
    auto v = dn.hasProfile() ? dn.profile() : std::vector<float>();
    if (v.empty()) v = storedProfile;
    if (! v.empty()) st.setProperty ("profile", Base64::toBase64 (v.data(), v.size() * sizeof (float)), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}
void GlassFrogProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = ValueTree::fromXml (*xml);
            storedProfile.clear();
            const String b64 = st.getProperty ("profile").toString();
            if (b64.isNotEmpty())
            {
                MemoryOutputStream mo;
                if (Base64::convertFromBase64 (mo, b64) && mo.getDataSize() % sizeof (float) == 0)
                {
                    storedProfile.resize (mo.getDataSize() / sizeof (float));
                    std::memcpy (storedProfile.data(), mo.getData(), mo.getDataSize());
                }
            }
            st.removeProperty ("profile", nullptr);
            apvts.replaceState (st);
            if ((int) storedProfile.size() == dn.bins()) { dn.setProfile (storedProfile); hasProfileFlag = true; }
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlassFrogProcessor(); }
