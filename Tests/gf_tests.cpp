// Maid Frog offline tests: a synthetic voice (harmonics with formants, fricatives, breath) in a room with noise, hum and hiss.
#include "../Source/DSP/Denoise.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
using namespace gf;
static int fails = 0, total = 0;
static void report (bool ok, const char* name, double v, const char* want) { ++total; if (! ok) ++fails; std::printf ("%s %-86s %9.2f  (%s)\n", ok ? "ok  " : "FAIL", name, v, want); }
static constexpr double SR = 48000.0, PI = 3.14159265358979323846;

struct Biq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bp (double f, double q) { const double w = 2 * PI * f / SR, al = std::sin (w) / (2 * q), a0 = 1 + al; b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * std::cos (w) / a0; a2 = (1 - al) / a0; }
    void lp (double f, double q) { const double w = 2 * PI * f / SR, al = std::sin (w) / (2 * q), a0 = 1 + al, c = std::cos (w); b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    double p (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };

struct Scene { std::vector<double> clean, noise, noisy, speechMask, fricMask; };

static Scene make (double seconds, double reverbT60 = 0.0, int seed = 1)
{
    std::mt19937 rng (seed); std::normal_distribution<double> nd (0, 1);
    const int n = (int) (seconds * SR);
    Scene s; s.clean.assign (n, 0.0); s.noise.assign (n, 0.0); s.speechMask.assign (n, 0.0); s.fricMask.assign (n, 0.0);
    // voice
    Biq f1, f2, f3, fr, fLow; fLow.lp (450, 0.7); f1.bp (650, 5); f2.bp (1500, 6); f3.bp (2900, 7); fr.bp (7000, 1.2);
    double ph = 0; const double t0 = 1.8;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR; double v = 0, fric = 0;
        if (t > t0)
        {
            const double u = std::fmod (t - t0, 0.9);                       // syllable of 0.9 s: voiced 0.55 s, fricative 0.1 s, pause
            const double f0 = 130 + 25 * std::sin (2 * PI * 0.7 * t) + 4 * std::sin (2 * PI * 5.2 * t);
            if (u < 0.55)
            {
                const double env = std::min (1.0, u / 0.03) * std::min (1.0, (0.55 - u) / 0.05);
                ph += f0 / SR; ph -= std::floor (ph);
                double e = 0;
                for (int h = 1; h <= 60; ++h) { if (f0 * h > 16000) break; e += std::sin (2 * PI * h * ph) / std::pow ((double) h, 1.0); }
                const double x = e * 0.04 + 0.002 * nd (rng);
                v = (f1.p (x) * 1.0 + f2.p (x) * 0.8 + f3.p (x) * 0.5 + 0.03 * x + 0.9 * fLow.p (x)) * env;   // a deep voice: strong fundamental and low harmonics
                s.speechMask[i] = 1.0;
            }
            else if (u < 0.65)
            {
                const double env = std::sin (PI * (u - 0.55) / 0.1);
                fric = fr.p (nd (rng)) * 0.012 * env; s.speechMask[i] = 1.0; s.fricMask[i] = 1.0;
            }
        }
        s.clean[i] = v * 0.9 + fric;
    }
    if (reverbT60 > 0)                                                       // exponential-noise room impulse response
    {
        const int L = (int) (reverbT60 * 1.2 * SR); std::vector<double> ir (L); const double a = 6.9078 / reverbT60;
        for (int i = 0; i < L; ++i) ir[i] = nd (rng) * std::exp (-a * i / SR) * (i == 0 ? 0.0 : 1.0);
        double e = 0; for (double x : ir) e += x * x; for (auto& x : ir) x *= 0.12 / std::sqrt (e / 1.0) * 1.0;
        std::vector<double> wet (n, 0.0);
        for (int i = 0; i < n; i += 1) { if (std::abs (s.clean[i]) < 1e-9) continue; for (int j = 0; j < L && i + j < n; j += 1) wet[i + j] += s.clean[i] * ir[j]; }
        for (int i = 0; i < n; ++i) s.clean[i] += wet[i];
    }
    // noise: pink-ish room (about -42 dBFS), hum 60 Hz + harmonics, hiss
    Biq lp; lp.lp (900, 0.7); double pk = 0;
    for (int i = 0; i < n; ++i)
    {
        const double room = lp.p (nd (rng)) * 0.012, hiss = nd (rng) * 0.0007, t = i / SR;
        const double hum = 0.0012 * (std::sin (2 * PI * 60 * t) + 0.5 * std::sin (2 * PI * 120 * t) + 0.25 * std::sin (2 * PI * 180 * t));
        s.noise[i] = room + hiss + hum; pk = std::max (pk, std::abs (s.noise[i]));
    }
    s.noisy.resize (n); for (int i = 0; i < n; ++i) s.noisy[i] = s.clean[i] + s.noise[i];
    return s;
}
struct Run { std::vector<double> out, dry; };
static Run run (const std::vector<double>& in, const Settings& st, double learnSeconds = 0.0, Denoiser** keep = nullptr)
{
    static Denoiser d; d.prepare (SR); d.setSettings (st);
    if (learnSeconds > 0) d.startLearn();
    Run r; r.out.resize (in.size()); r.dry.resize (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (learnSeconds > 0 && i == (size_t) (learnSeconds * SR)) d.stopLearn();
        double l = in[i], rr = in[i]; d.processFrame (l, rr); r.out[i] = l; r.dry[i] = d.delayedL();
    }
    (void) keep; return r;
}
static double rms (const std::vector<double>& x, double a, double b)
{
    double s = 0; int c = 0; for (int i = (int) (a * SR); i < (int) (b * SR) && i < (int) x.size(); ++i) { s += x[i] * x[i]; ++c; } return std::sqrt (s / std::max (1, c));
}
static double db (double x) { return 20 * std::log10 (std::max (x, 1e-12)); }
static std::vector<double> hp (const std::vector<double>& x, double f)       // HF band: 4th-order high-pass at f
{
    std::vector<double> y = x; for (int k = 0; k < 2; ++k) { Biq b; const double w = 2 * PI * f / SR, al = std::sin (w) / 1.414, c = std::cos (w), a0 = 1 + al; b.b0 = (1 + c) / 2 / a0; b.b1 = -(1 + c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; for (auto& v : y) v = b.p (v); } return y;
}
// energy of x (shifted by latency) over the speech frames only
static double speechEnergy (const std::vector<double>& x, const std::vector<double>& mask, int lat)
{
    double e = 0; for (size_t i = 0; i + lat < x.size(); ++i) if (mask[i] > 0.5) e += x[i + lat] * x[i + lat]; return e;
}
static double snr (const std::vector<double>& est, const std::vector<double>& ref, int lat, double from)
{
    double s = 0, e = 0; for (size_t i = (size_t) (from * SR); i + lat < est.size(); ++i) { const double d = est[i + lat] - ref[i]; s += ref[i] * ref[i]; e += d * d; } return 10 * std::log10 (s / std::max (e, 1e-18));
}

int main (int argc, char** argv)
{
    std::printf ("Maid Frog tests\n");
    Denoiser probe; probe.prepare (SR);
    report (probe.latency() == 2048, "latency at 48 kHz is 2048 samples (about 43 ms)", probe.latency(), "= 2048");
    const int lat = 2048;
    auto sc = make (14.0);
    Settings def;                                                                   // factory settings
    auto a = run (sc.noisy, def);
    bool fin = true, clip = true; for (double v : a.out) { if (! std::isfinite (v)) fin = false; if (std::abs (v) > 1.0) clip = false; }
    report (fin && clip, "output is finite and under 0 dBFS", fin && clip ? 1 : 0, "= 1");

    const double inPause = db (rms (sc.noisy, 0.6, 1.7)), pause_a = db (rms (a.out, 0.6 + lat / SR, 1.7 + lat / SR));
    // pause between syllables: u in 0.65..0.9 of the cycle. choose cycle 2 (t = 1.8 + 0.9*k)
    auto pauseRms = [&] (const std::vector<double>& x, int cyc) { const double t = 1.8 + 0.9 * cyc; return db (rms (x, t + 0.72, t + 0.88)); };
    std::vector<double> shifted (a.out.size(), 0.0); for (size_t i = 0; i + lat < a.out.size(); ++i) shifted[i] = a.out[i + lat];
    const double redPause = pauseRms (sc.noisy, 6) - pauseRms (shifted, 6);
    if (std::getenv("GFDBG")) for (double x0 = 0.0; x0 < 1.7; x0 += 0.2) std::printf ("  t %.1f in %.1f out %.1f\n", x0, db (rms (sc.noisy, x0, x0 + 0.2)), db (rms (shifted, x0, x0 + 0.2)));
    std::printf ("      noise only before the voice: in %.1f dBFS, out %.1f dBFS; pause between syllables: reduction %.1f dB\n", inPause, pause_a, redPause);
    report (pause_a < inPause - 8.0, "default: the noise before the voice drops by 8 dB or more (dB)", inPause - pause_a, ">= 8");
    report (redPause > 16.0, "default: the pauses between syllables drop by 16 dB or more (dB)", redPause, ">= 16");

    const double imp = snr (a.out, sc.clean, lat, 4.0) - snr (std::vector<double> (sc.noisy.begin() + lat, sc.noisy.end()), sc.clean, 0, 4.0);
    std::printf ("      SNR in: %.1f dB, out: %.1f dB\n", snr (std::vector<double> (sc.noisy.begin() + lat, sc.noisy.end()), sc.clean, 0, 4.0) - 0.0, snr (a.out, sc.clean, lat, 4.0));
    report (imp > 3.0, "SNR improves against the clean voice (dB)", imp, "> 3");

    // darkness: the energy above 5 kHz during speech, against the clean voice
    auto hfClean = hp (sc.clean, 5000.0), hfA = hp (a.out, 5000.0);
    const double hfRatioAir = 10 * std::log10 (speechEnergy (hfA, sc.speechMask, lat) / speechEnergy (hfClean, sc.speechMask, 0));
    Settings noAir = def; noAir.air = 0.0;
    auto b = run (sc.noisy, noAir); auto hfB = hp (b.out, 5000.0);
    const double hfRatioNo = 10 * std::log10 (speechEnergy (hfB, sc.speechMask, lat) / speechEnergy (hfClean, sc.speechMask, 0));
    std::printf ("      energy above 5 kHz in speech vs the clean voice: Air 0.6 %.2f dB, Air 0 %.2f dB\n", hfRatioAir, hfRatioNo);
    report (std::abs (hfRatioAir) < 1.5, "Air 0.6: the top of the voice stays within 1.5 dB of the clean one (dB)", hfRatioAir, "|x| < 1.5");
    Settings hard = def; hard.reductionDb = 40; hard.sensitivity = 0.9; hard.air = 0.0;
    auto h0 = run (sc.noisy, hard); hard.air = 1.0; auto h1 = run (sc.noisy, hard);
    const double hf0 = 10 * std::log10 (speechEnergy (hp (h0.out, 5000), sc.speechMask, lat) / speechEnergy (hfClean, sc.speechMask, 0)),
                 hf1 = 10 * std::log10 (speechEnergy (hp (h1.out, 5000), sc.speechMask, lat) / speechEnergy (hfClean, sc.speechMask, 0));
    std::printf ("      aggressive (40 dB, sensitivity 0.9): top of the voice %.2f dB with Air 0, %.2f dB with Air 1\n", hf0, hf1);
    report (hf1 > hf0 + 0.8 || hf0 > -0.5, "Air keeps more of the top than no Air when the settings are aggressive (dB gained)", hf1 - hf0, "> 0.8 or nothing lost");

    // body: the low harmonics (above the rumble) of a deep voice must stay
    auto lowBand = [&] (const std::vector<double>& x) { auto h = hp (x, 80.0), h2 = hp (x, 450.0); std::vector<double> o (x.size()); for (size_t i = 0; i < x.size(); ++i) o[i] = h[i] - h2[i]; return o; };
    std::vector<double> quiet (sc.noisy.size()); for (size_t i = 0; i < quiet.size(); ++i) quiet[i] = sc.clean[i] + 0.3 * sc.noise[i];
    auto qa = run (quiet, def);
    const double lbRatio = 10 * std::log10 (speechEnergy (lowBand (qa.out), sc.speechMask, lat) / speechEnergy (lowBand (sc.clean), sc.speechMask, 0));
    Settings noBody = def; noBody.body = 0.0; auto nb = run (quiet, noBody);
    const double lbNo = 10 * std::log10 (speechEnergy (lowBand (nb.out), sc.speechMask, lat) / speechEnergy (lowBand (sc.clean), sc.speechMask, 0));
    { Settings off = def; off.enabled = false; auto q0 = run (quiet, off); std::printf ("      [metric calibration, plugin off] %.2f dB\n", 10 * std::log10 (speechEnergy (lowBand (q0.out), sc.speechMask, lat) / speechEnergy (lowBand (sc.clean), sc.speechMask, 0))); }
    std::printf ("      80-450 Hz in speech vs the clean voice: Body 0.9 %.2f dB, Body 0 %.2f dB\n", lbRatio, lbNo);
    report (std::abs (lbRatio) < 1.5, "Body 0.9: the low harmonics of the voice stay within 1.5 dB of the clean one (dB)", lbRatio, "|x| < 1.5");

    // low cut: a steady 30 Hz rumble goes away faster than with the cut off, and 100 Hz does not move
    {
        auto toneOut = [&] (double fr, double cut)
        {
            Denoiser d; d.prepare (SR); Settings st = def; st.lowCut = cut; d.setSettings (st);
            double ei = 0, eo = 0; const int n = (int) (SR * 2.0);
            for (int i = 0; i < n; ++i)
            {
                const double x = 0.05 * std::sin (2.0 * PI * fr * i / SR) + 1e-5 * (double) ((i * 7919) % 1000 - 500) / 500.0;
                double a2 = x, b2 = x; d.processFrame (a2, b2);
                if (i > (int) (SR * 0.2)) ei += x * x;
                if (i > (int) (SR * 0.2) + d.latency()) eo += a2 * a2;
            }
            return 10 * std::log10 (eo / ei);
        };
        const double r30on = toneOut (30.0, 55.0), r30off = toneOut (30.0, 0.0), r100on = toneOut (100.0, 55.0), r100off = toneOut (100.0, 0.0);
        std::printf ("      tone level change: 30 Hz cut on %.1f dB / off %.1f dB; 100 Hz cut on %.1f dB / off %.1f dB\n", r30on, r30off, r100on, r100off);
        report (r30off - r30on > 8.0, "Low Cut 55 Hz takes a 30 Hz rumble down at least 8 dB more than with it off (dB)", r30off - r30on, "> 8");
        report (std::abs (r100on - r100off) < 0.5, "Low Cut 55 Hz does not touch 100 Hz (dB)", r100on - r100off, "|x| < 0.5");
    }
    // learned profile only (adaptive off)
    Settings lrn = def; lrn.adaptive = false;
    auto c = run (sc.noisy, lrn, 1.4);
    std::vector<double> sc2 (c.out.size(), 0.0); for (size_t i = 0; i + lat < c.out.size(); ++i) sc2[i] = c.out[i + lat];
    const double redLearn = pauseRms (sc.noisy, 6) - pauseRms (sc2, 6);
    report (redLearn > 16.0, "Learn (adaptive off): the pauses drop by 16 dB or more (dB)", redLearn, ">= 16");
    // hum: the 60 Hz tone must go (before the voice)
    auto tone = [&] (const std::vector<double>& x, double f, double from, double to) { double cs = 0, sn = 0; int cnt = 0; for (int i = (int) (from * SR); i < (int) (to * SR); ++i, ++cnt) { cs += x[i] * std::cos (2 * PI * f * i / SR); sn += x[i] * std::sin (2 * PI * f * i / SR); } return db (std::sqrt (cs * cs + sn * sn) * 2 / std::max (1, cnt)); };
    report (tone (shifted, 60.0, 0.6, 1.7) < tone (sc.noisy, 60.0, 0.6, 1.7) - 10.0, "the 60 Hz hum drops by 10 dB or more (dB)", tone (sc.noisy, 60.0, 0.6, 1.7) - tone (shifted, 60.0, 0.6, 1.7), ">= 10");

    // delta: processed + delta = delayed dry
    Settings dl = def; dl.delta = true; auto d = run (sc.noisy, dl);
    double me = 0; for (size_t i = 0; i < a.out.size(); ++i) me = std::max (me, std::abs (a.out[i] + d.out[i] - a.dry[i]));
    report (me < 1e-9, "delta: processed + delta = the delayed input (max error)", me, "< 1e-9");

    // room: tail between syllables with a reverberant voice
    auto rev = make (14.0, 0.5, 3);
    Settings r0 = def, r1 = def; r1.room = 0.7;
    auto ra = run (rev.noisy, r0), rb = run (rev.noisy, r1);
    std::vector<double> sa (ra.out.size(), 0), sb (rb.out.size(), 0); for (size_t i = 0; i + lat < ra.out.size(); ++i) { sa[i] = ra.out[i + lat]; sb[i] = rb.out[i + lat]; }
    auto tail = [&] (const std::vector<double>& x) { double e = 0; for (int cyc = 2; cyc < 12; ++cyc) { const double t = 1.8 + 0.9 * cyc; e += rms (x, t + 0.30, t + 0.45); } return db (e / 10); };
    std::printf ("      reverb tail between syllables: Room 0 %.1f dBFS, Room 0.7 %.1f dBFS\n", tail (sa), tail (sb));
    report (tail (sb) < tail (sa) - 1.5, "Room 0.7 takes at least 1.5 dB more of the reverb tail away than Room 0 (dB)", tail (sa) - tail (sb), ">= 1.5");
    const double voiceKeep = db (rms (sb, 1.8 + 9 * 0.9 + 0.05, 1.8 + 9 * 0.9 + 0.5)) - db (rms (sa, 1.8 + 9 * 0.9 + 0.05, 1.8 + 9 * 0.9 + 0.5));
    report (voiceKeep > -4.0, "Room 0.7 does not eat the voice itself (level of the vowels vs Room 0, dB)", voiceKeep, "> -4");

    // stereo: both channels get the same gain
    { Denoiser st; st.prepare (SR); st.setSettings (def); double maxd = 0;
      for (size_t i = 0; i < sc.noisy.size(); ++i) { double l = sc.noisy[i], rr = 0.5 * sc.noisy[i]; st.processFrame (l, rr); if (i > 20000) maxd = std::max (maxd, std::abs (rr - 0.5 * l)); }
      report (maxd < 1e-9, "stereo: a 2:1 level difference between the channels stays 2:1 (max error)", maxd, "< 1e-9"); }
    // profile round trip
    { Denoiser p1, p2; p1.prepare (SR); p2.prepare (SR); p1.setSettings (def); p2.setSettings (def); p1.startLearn();
      for (int i = 0; i < (int) (1.4 * SR); ++i) { double l = sc.noisy[i], rr = l; p1.processFrame (l, rr); }
      p1.stopLearn(); { double l = 0, rr = 0; for (int i = 0; i < 4096; ++i) p1.processFrame (l, rr); }
      auto v = p1.profile(); p2.setProfile (v); { double l = 0, rr = 0; for (int i = 0; i < 4096; ++i) p2.processFrame (l, rr); }
      report (p2.hasProfile() && v.size() == 1025, "profile: it can be saved and restored (bins)", (double) v.size(), "= 1025"); }
    // other sample rates: no crash, finite
    for (double rate : { 44100.0, 96000.0 })
    { Denoiser x; x.prepare (rate); x.setSettings (def); bool ok = true; std::mt19937 g (5); std::normal_distribution<double> n (0, 0.05);
      for (int i = 0; i < (int) (rate * 2); ++i) { double l = n (g), rr = l; x.processFrame (l, rr); if (! std::isfinite (l)) ok = false; }
      char nm[80]; std::snprintf (nm, sizeof nm, "%.0f Hz: finite output (latency %d)", rate, x.latency()); report (ok, nm, ok ? 1 : 0, "= 1"); }
    // speed
    { Denoiser x; x.prepare (SR); x.setSettings (def); const int n = (int) (SR * 10); const auto t0 = std::chrono::steady_clock::now();
      for (int i = 0; i < n; ++i) { double l = sc.noisy[i % sc.noisy.size()], rr = l; x.processFrame (l, rr); }
      const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
      report (sec < 10.0 * 0.12, "speed: 10 s of stereo audio takes less than 12 % of real time (% of real time)", sec / 10.0 * 100.0, "< 12"); }
    if (argc > 1)   // demo: write input, output and delta as 16-bit WAV
    {
        auto wr = [&] (const std::string& fn, const std::vector<double>& x, int skip) { FILE* f = std::fopen (fn.c_str(), "wb"); const int n = (int) x.size() - skip; const int dsz = n * 2; const int sr = (int) SR, br = sr * 2; const short ch = 1, ba = 2, bits = 16, fmt = 1; const int fs = 16;
            std::fwrite ("RIFF", 1, 4, f); int t = 36 + dsz; std::fwrite (&t, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&fs, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f); std::fwrite (&sr, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dsz, 4, 1, f);
            for (int i = 0; i < n; ++i) { const short s = (short) std::lround (std::clamp (x[i + skip] * 1.0, -1.0, 1.0) * 32767.0); std::fwrite (&s, 2, 1, f); } std::fclose (f); };
        const std::string d0 = argv[1]; wr (d0 + "/maidfrog_demo_noisy.wav", sc.noisy, 0); wr (d0 + "/maidfrog_demo_processed.wav", a.out, lat); wr (d0 + "/maidfrog_demo_removed_delta.wav", d.out, lat);
        wr (d0 + "/maidfrog_demo_clean_reference.wav", sc.clean, 0);
    }
    std::printf ("\n%d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
