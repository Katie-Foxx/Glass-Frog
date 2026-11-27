#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout MaidFrogProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float def, const char* unit = "")
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.01f), def, AudioParameterFloatAttributes().withLabel (unit)));
    };
    flt (gfid::reduction, "Reduction", 0.0f, 60.0f, 18.0f, " dB");
    flt (gfid::sensitivity, "Sensitivity", 0.0f, 1.0f, 0.4f);
    flt (gfid::air, "Air", 0.0f, 1.0f, 0.7f);
    flt (gfid::body, "Body", 0.0f, 1.0f, 0.9f);
    flt (gfid::lowcut, "Low Cut", 0.0f, 120.0f, 55.0f, " Hz");
    flt (gfid::room, "Room", 0.0f, 1.0f, 0.0f);
    flt (gfid::smooth, "Smooth", 0.0f, 1.0f, 0.6f);
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { gfid::adaptive, 1 }, "Adaptive", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { gfid::delta, 1 }, "Listen to Removed", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { gfid::power, 1 }, "Power", true));
    return { p.begin(), p.end() };
}

MaidFrogProcessor::MaidFrogProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MaidFrog", createLayout())
{
    rescanLibrary();
}

void MaidFrogProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    dn.prepare (sr);
    setLatencySamples (dn.latency());
    learningFlag = false;
    { const ScopedLock sl (libLock); applyActive(); hasProfileFlag = activeValid; }
}

void MaidFrogProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), ch = buffer.getNumChannels();
    for (int c = getTotalNumInputChannels(); c < ch; ++c) buffer.clear (c, 0, n);

    switch (cmd.exchange (0))
    {
        case 1: dn.startLearn(); break;
        case 2: dn.stopLearn(); break;
        case 3: dn.stopLearn(); dn.clearProfile(); break;
        case 4: { const double d = dn.floorOffsetDb(); if (std::isfinite (d)) { matchDb.store ((float) d); matchState.store (2); } else matchState.store (3); break; }
        default: break;
    }
    gf::Settings s;
    s.reductionDb = get (gfid::reduction); s.sensitivity = get (gfid::sensitivity); s.air = get (gfid::air); s.body = get (gfid::body); s.lowCut = get (gfid::lowcut);
    s.room = get (gfid::room); s.smooth = get (gfid::smooth);
    s.adaptive = get (gfid::adaptive) > 0.5f; s.delta = get (gfid::delta) > 0.5f; s.enabled = get (gfid::power) > 0.5f;
    dn.setSettings (s);

    auto* l = buffer.getWritePointer (0);
    auto* r = ch > 1 ? buffer.getWritePointer (1) : nullptr;
    const bool wasLearning = learningFlag.load();
    for (int i = 0; i < n; ++i)
    {
        double a = l[i], b = r ? (double) r[i] : a;
        dn.processFrame (a, b);
        l[i] = (float) a; if (r) r[i] = (float) b;
    }
    learningFlag = dn.isLearning();
    if (wasLearning && ! learningFlag.load() && dn.learnedFrames() >= 4) learnDone = true;
    hasProfileFlag = dn.hasProfile();
}

AudioProcessorEditor* MaidFrogProcessor::createEditor() { return new MaidFrogEditor (*this); }

// ---- noise library ---------------------------------------------------------------------------------------------------
File MaidFrogProcessor::libraryDir()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Lismok").getChildFile ("Maid Frog").getChildFile ("Noise Profiles");
}

static String bytesToB64 (const std::vector<float>& v) { return Base64::toBase64 (v.data(), v.size() * sizeof (float)); }
static bool b64ToFloats (const String& b64, std::vector<float>& out)
{
    MemoryOutputStream mo;
    if (b64.isEmpty() || ! Base64::convertFromBase64 (mo, b64) || mo.getDataSize() % sizeof (float) != 0) return false;
    out.resize (mo.getDataSize() / sizeof (float));
    std::memcpy (out.data(), mo.getData(), mo.getDataSize());
    return true;
}

void MaidFrogProcessor::writeEntry (const NoiseEntry& e)
{
    auto dir = libraryDir();
    if (! dir.createDirectory()) return;
    XmlElement x ("NoiseProfile");
    x.setAttribute ("name", e.name); x.setAttribute ("N", e.N); x.setAttribute ("sr", e.sr); x.setAttribute ("gain", e.gainDb);
    x.setAttribute ("data", bytesToB64 (e.data));
    x.writeTo (dir.getChildFile (File::createLegalFileName (e.name) + ".noiseprofile"));
}

bool MaidFrogProcessor::readEntry (const File& f, NoiseEntry& e)
{
    if (auto x = parseXML (f))
        if (x->hasTagName ("NoiseProfile"))
        {
            e.name = x->getStringAttribute ("name"); e.N = x->getIntAttribute ("N"); e.sr = x->getDoubleAttribute ("sr");
            e.gainDb = x->getDoubleAttribute ("gain");
            return e.name.isNotEmpty() && e.N >= 2 && b64ToFloats (x->getStringAttribute ("data"), e.data) && (int) e.data.size() == e.N / 2 + 1;
        }
    return false;
}

void MaidFrogProcessor::rescanLibrary()
{
    std::vector<NoiseEntry> v;
    for (auto& f : libraryDir().findChildFiles (File::findFiles, false, "*.noiseprofile"))
    {
        NoiseEntry e;
        if (readEntry (f, e)) v.push_back (std::move (e));
    }
    std::sort (v.begin(), v.end(), [] (const NoiseEntry& a, const NoiseEntry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    const ScopedLock sl (libLock);
    String keep = activeIdx >= 0 && activeIdx < (int) library.size() ? library[(size_t) activeIdx].name : String();
    library = std::move (v);
    activeIdx = -1;
    if (keep.isNotEmpty())
        for (int i = 0; i < (int) library.size(); ++i)
            if (library[(size_t) i].name == keep) activeIdx = i;
}

std::vector<MaidFrogProcessor::NoiseInfo> MaidFrogProcessor::libraryInfo() const
{
    const ScopedLock sl (libLock);
    std::vector<NoiseInfo> v;
    for (auto& e : library) v.push_back ({ e.name, e.gainDb });
    return v;
}
int MaidFrogProcessor::activeIndex() const { const ScopedLock sl (libLock); return activeIdx; }
bool MaidFrogProcessor::hasActive() const { const ScopedLock sl (libLock); return activeValid; }
String MaidFrogProcessor::activeName() const { const ScopedLock sl (libLock); return activeValid ? active.name : String(); }
double MaidFrogProcessor::activeGainDb() const { const ScopedLock sl (libLock); return active.gainDb; }

// the active profile goes to the denoiser at the current frame size and sample rate, with its level trim (libLock held)
void MaidFrogProcessor::applyActive()
{
    if (! activeValid) return;
    auto v = gf::Denoiser::convertProfile (active.data, active.N, active.sr, dn.bins(), sr, active.gainDb);
    if ((int) v.size() == dn.bins()) dn.setProfile (v);
}

void MaidFrogProcessor::requestClear()
{
    cmd.store (3);
    const ScopedLock sl (libLock);
    activeValid = false; activeIdx = -1; active = {};
}

void MaidFrogProcessor::syncLearned()
{
    if (! learnDone.exchange (false)) return;
    auto v = dn.profile();
    if ((int) v.size() != dn.bins()) { learnDone = true; return; }
    const ScopedLock sl (libLock);
    active.data = std::move (v); active.N = dn.frameSize(); active.sr = sr; active.gainDb = 0.0; active.name = "Learned";
    activeValid = true; activeIdx = -1;
}

bool MaidFrogProcessor::saveCurrent (const String& nameIn)
{
    syncLearned();
    const String name = nameIn.trim();
    const ScopedLock sl (libLock);
    if (! activeValid || name.isEmpty()) return false;
    NoiseEntry e = active; e.name = name;
    int at = -1;
    for (int i = 0; i < (int) library.size(); ++i) if (library[(size_t) i].name.equalsIgnoreCase (name)) at = i;
    if (at >= 0) library[(size_t) at] = e; else library.push_back (e);
    std::sort (library.begin(), library.end(), [] (const NoiseEntry& a, const NoiseEntry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (int i = 0; i < (int) library.size(); ++i) if (library[(size_t) i].name == name) activeIdx = i;
    active.name = name;
    writeEntry (e);
    return true;
}

void MaidFrogProcessor::selectEntry (int i)
{
    const ScopedLock sl (libLock);
    if (i < 0 || i >= (int) library.size()) return;
    active = library[(size_t) i]; activeValid = true; activeIdx = i;
    applyActive();
}

void MaidFrogProcessor::stepEntry (int delta)
{
    int n, cur;
    { const ScopedLock sl (libLock); n = (int) library.size(); cur = activeIdx; }
    if (n == 0) return;
    selectEntry (cur < 0 ? (delta > 0 ? 0 : n - 1) : (cur + delta + n) % n);
}

void MaidFrogProcessor::setActiveGainDb (double db, bool commit)
{
    const ScopedLock sl (libLock);
    if (! activeValid) return;
    active.gainDb = jlimit (-36.0, 36.0, db);
    applyActive();
    if (activeIdx >= 0 && activeIdx < (int) library.size())
    {
        library[(size_t) activeIdx].gainDb = active.gainDb;
        if (commit) writeEntry (library[(size_t) activeIdx]);
    }
}

void MaidFrogProcessor::renameEntry (int i, const String& nameIn)
{
    const String name = nameIn.trim();
    const ScopedLock sl (libLock);
    if (i < 0 || i >= (int) library.size() || name.isEmpty()) return;
    for (int j = 0; j < (int) library.size(); ++j) if (j != i && library[(size_t) j].name.equalsIgnoreCase (name)) return;
    libraryDir().getChildFile (File::createLegalFileName (library[(size_t) i].name) + ".noiseprofile").deleteFile();
    library[(size_t) i].name = name;
    writeEntry (library[(size_t) i]);
    const bool wasActive = i == activeIdx;
    std::sort (library.begin(), library.end(), [] (const NoiseEntry& a, const NoiseEntry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    if (wasActive) { active.name = name; for (int j = 0; j < (int) library.size(); ++j) if (library[(size_t) j].name == name) activeIdx = j; }
    else activeIdx = -1, activeIdx = [&] { for (int j = 0; j < (int) library.size(); ++j) if (activeValid && library[(size_t) j].name == active.name) return j; return -1; }();
}

void MaidFrogProcessor::deleteEntry (int i)
{
    const ScopedLock sl (libLock);
    if (i < 0 || i >= (int) library.size()) return;
    libraryDir().getChildFile (File::createLegalFileName (library[(size_t) i].name) + ".noiseprofile").deleteFile();
    const String gone = library[(size_t) i].name;
    library.erase (library.begin() + i);
    activeIdx = -1;
    if (activeValid)
        for (int j = 0; j < (int) library.size(); ++j) if (library[(size_t) j].name == active.name) activeIdx = j;
    juce::ignoreUnused (gone);
}

int MaidFrogProcessor::pollMatch()
{
    const int st = matchState.load();
    if (st == 2) { matchState = 0; setActiveGainDb (activeGainDb() + matchDb.load(), true); return 2; }
    if (st == 3) { matchState = 0; return 3; }
    return 0;
}

// ---- state: the profile in use travels with the project (the library itself lives in files) -------------------------------------
void MaidFrogProcessor::getStateInformation (MemoryBlock& dest)
{
    syncLearned();
    auto st = apvts.copyState();
    {
        const ScopedLock sl (libLock);
        if (activeValid && ! active.data.empty())
        {
            st.setProperty ("profile", bytesToB64 (active.data), nullptr);
            st.setProperty ("profN", active.N, nullptr); st.setProperty ("profSr", active.sr, nullptr);
            st.setProperty ("profGain", active.gainDb, nullptr); st.setProperty ("profName", active.name, nullptr);
            st.setProperty ("profLinked", activeIdx >= 0, nullptr);
        }
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}
void MaidFrogProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = ValueTree::fromXml (*xml);
            NoiseEntry e;
            bool ok = b64ToFloats (st.getProperty ("profile").toString(), e.data);
            if (ok)
            {
                e.N = (int) st.getProperty ("profN", (int) (e.data.size() - 1) * 2);           // old projects: the size tells the frame
                e.sr = (double) st.getProperty ("profSr", 0.0);
                e.gainDb = (double) st.getProperty ("profGain", 0.0);
                e.name = st.getProperty ("profName", "Learned").toString();
                ok = e.N >= 2 && (int) e.data.size() == e.N / 2 + 1;
            }
            const bool linked = (bool) st.getProperty ("profLinked", false);
            for (auto* k : { "profile", "profN", "profSr", "profGain", "profName", "profLinked" }) st.removeProperty (k, nullptr);
            apvts.replaceState (st);
            const ScopedLock sl (libLock);
            activeValid = ok; activeIdx = -1;
            if (ok)
            {
                active = std::move (e);
                if (linked)
                    for (int i = 0; i < (int) library.size(); ++i) if (library[(size_t) i].name == active.name) activeIdx = i;
                applyActive();
                hasProfileFlag = true;
            }
            else { cmd.store (3); hasProfileFlag = false; }
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MaidFrogProcessor(); }
