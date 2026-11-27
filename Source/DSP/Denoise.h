#pragma once
// Glass Frog - noise reduction for voice, in the spectral domain (STFT, N = 40 ms, 75 % overlap, sqrt-Hann windows).
//
//  * Noise estimate: a profile learned from a noise-only passage (Learn) and / or an adaptive tracker that follows the floor of every
//    bin (it falls at once and rises slowly, so speech does not feed it), smoothed across frequency.
//  * Room: the late reverberation of the speech itself is estimated from the smoothed power of 60 ms earlier (exponential decay model)
//    and counted as noise, so the tail between words and syllables goes away.
//  * Gain: decision-directed a-priori SNR (Ephraim-Malah) and a Wiener gain with an over-subtraction factor (Sensitivity), floored
//    at the chosen Reduction; smoothed across bins and with a slower closing than opening against musical noise.
//  * Air: the usual reason these plugins sound dark is that every weak consonant and the breath of the voice are treated as noise.
//    While speech is present (broadband SNR, and the 4-10 kHz SNR for sibilants) the reduction above ~3 kHz is scaled down, so
//    the top of the voice stays; in the pauses the full reduction applies, so the hiss still goes.
//  * The same real gain goes to both channels (the stereo image does not move). Fixed latency of N samples, reported to the host.
//  * Delta gives what was taken away (delayed dry minus processed).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <mutex>
#include <vector>

namespace gf
{
struct Settings
{
    double reductionDb = 18.0;     // maximum reduction, 0..60
    double sensitivity = 0.4;      // 0..1: how much counts as noise
    double air = 0.7;              // 0..1: keeps the top of the voice while it speaks
    double lowCut = 55.0;          // Hz: below this the rumble is removed (smooth, linear phase); 0 = off
    double body = 0.9;             // 0..1: keeps the low harmonics (70-450 Hz) of a deep voice while it speaks
    double room = 0.0;             // 0..1: takes the tail of the room away
    double smooth = 0.6;           // 0..1: fewer artefacts (slower) <-> faster
    bool adaptive = true;          // follow the noise floor while playing
    bool enabled = true;
    bool delta = false;
};

class Denoiser
{
public:
    static constexpr int kDisplay = 96;      // log-spaced display bands, 30 Hz .. 20 kHz

    static int frameFor (double sr) noexcept { int n = 256; while ((double) n < sr * 0.04 && n < 16384) n <<= 1; return n; }
    int latency() const noexcept { return N; }
    int frameSize() const noexcept { return N; }

    void prepare (double sampleRate)
    {
        sr = sampleRate; N = frameFor (sr); H = N / 4; K = N / 2 + 1;
        inL.assign ((size_t) N, 0.0); inR.assign ((size_t) N, 0.0); accL.assign ((size_t) N, 0.0); accR.assign ((size_t) N, 0.0);
        re.assign ((size_t) N, 0.0); im.assign ((size_t) N, 0.0); win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) N));
        tw.resize ((size_t) N / 2);
        for (int i = 0; i < N / 2; ++i) tw[(size_t) i] = std::polar (1.0, -2.0 * kPi * (double) i / (double) N);
        rev.resize ((size_t) N); int bits = 0; while ((1 << bits) < N) ++bits;
        for (int i = 0; i < N; ++i) { int x = i, r = 0; for (int b = 0; b < bits; ++b) { r = (r << 1) | (x & 1); x >>= 1; } rev[(size_t) i] = r; }
        const size_t k = (size_t) K;
        P.assign (k, 0.0); Ps.assign (k, 0.0); Pa.assign (k, 0.0); Pn.assign (k, 0.0); G.assign (k, 1.0); Gs.assign (k, 1.0); Gtmp.assign (k, 1.0);
        scratch.assign (k, 0.0); prevSnrPow.assign (k, 0.0); pr.assign (k, 0.0); Gtmp2.assign (k, 1.0); prof.assign (k, 0.0); profLive.assign (k, 0.0); lateP.assign (k, 0.0);
        const double dt = (double) H / sr;
        histLen = std::max (2, (int) std::lround (0.06 / dt));
        hist.assign ((size_t) histLen * k, 0.0); histPos = 0;
        subLen = std::max (4, (int) std::lround (0.3 / dt));
        subMin.assign ((size_t) kSub * k, 0.0); curMin.assign (k, 0.0); subPos = 0; subCount = 0;
        hasProf = false; learning = false; learnCount = 0;
        reset();
        for (int d = 0; d < kDisplay; ++d)
        {
            const double f0 = 30.0 * std::pow (20000.0 / 30.0, (double) d / kDisplay), f1 = 30.0 * std::pow (20000.0 / 30.0, (double) (d + 1) / kDisplay);
            dispLo[d] = std::clamp ((int) std::floor (f0 / (sr / N)), 1, K - 1); dispHi[d] = std::clamp ((int) std::ceil (f1 / (sr / N)), dispLo[d], K - 1);
        }
    }

    void reset() noexcept
    {
        std::fill (inL.begin(), inL.end(), 0.0); std::fill (inR.begin(), inR.end(), 0.0);
        std::fill (accL.begin(), accL.end(), 0.0); std::fill (accR.begin(), accR.end(), 0.0);
        std::fill (Gs.begin(), Gs.end(), 1.0); std::fill (prevSnrPow.begin(), prevSnrPow.end(), 0.0);
        std::fill (hist.begin(), hist.end(), 0.0); std::fill (Ps.begin(), Ps.end(), 0.0); std::fill (Pa.begin(), Pa.end(), 0.0);
        std::fill (subMin.begin(), subMin.end(), 0.0); std::fill (curMin.begin(), curMin.end(), 0.0); subPos = 0; subCount = 0;
        floorValid = false; w = 0; count = 0; frames = 0; dryL = dryR = 0.0; speech = 0.0; speechHf = 0.0;
    }

    void setSettings (const Settings& s) { settings = s; }
    void startLearn() { std::fill (profLive.begin(), profLive.end(), 0.0); learnCount = 0; learning = true; }
    void stopLearn() { learning = false; }
    bool isLearning() const noexcept { return learning; }
    bool hasProfile() const noexcept { return hasProf; }
    void clearProfile() { hasProf = false; std::fill (prof.begin(), prof.end(), 0.0); }
    // Profile for the state of the plugin (power per bin, as floats). Set from the message thread; the audio thread takes it at the next frame.
    std::vector<float> profile() const { std::lock_guard<std::mutex> g (mtx); return snapshot; }
    void setProfile (const std::vector<float>& v)
    {
        std::lock_guard<std::mutex> g (mtx);
        if ((int) v.size() == K) { pending = v; havePending = true; }
    }
    int bins() const noexcept { return K; }
    int learnedFrames() const noexcept { return learnCount; }

    // Match: how many dB the noise floor that is being heard now (the adaptive tracker, 100 Hz - 9 kHz, median over the bins) lies above the
    // profile in use. NaN while there is no profile or no floor has been measured yet. Audio thread.
    double floorOffsetDb() noexcept
    {
        if (! hasProf || ! floorValid || scratch.empty()) return std::nan ("");
        const double fBin = sr / (double) N;
        int n = 0;
        for (int k = 1; k < K; ++k)
        {
            const double f = k * fBin;
            if (f < 100.0 || f > 9000.0) continue;
            const double a = std::max (Pa[(size_t) k] * kFloorBias, 1.0e-18), b = std::max (prof[(size_t) k], 1.0e-18);
            scratch[(size_t) n++] = 10.0 * std::log10 (a / b);
        }
        if (n < 8) return std::nan ("");
        std::nth_element (scratch.begin(), scratch.begin() + n / 2, scratch.begin() + n);
        return scratch[(size_t) (n / 2)] + 3.1;      // calibrated on stationary noise: the follower reads about 3.1 dB low
    }

    // A profile taken at another frame size / sample rate (and with a level trim in dB) for this one: interpolated in dB along the frequency axis.
    // The power per bin of a noise of fixed density goes with sr / N, which is what the factor accounts for.
    static std::vector<float> convertProfile (const std::vector<float>& src, int nOld, double srOld, int nNew, double srNew, double gainDb)
    {
        std::vector<float> out;
        if (src.size() < 2 || nOld < 2 || nNew < 2 || srNew <= 0.0) return out;
        if (srOld <= 0.0) srOld = srNew;
        const int kNew = nNew / 2 + 1, kOld = (int) src.size();
        out.resize ((size_t) kNew);
        const double scale = (srNew * nOld) / ((double) nNew * srOld) * std::pow (10.0, gainDb / 10.0);
        for (int k = 0; k < kNew; ++k)
        {
            const double x = std::clamp ((double) k * srNew / nNew * nOld / srOld, 0.0, (double) (kOld - 1));
            const int i = std::min ((int) x, kOld - 2);
            const double fr = x - i;
            const double a = std::log (std::max ((double) src[(size_t) i], 1.0e-20)), b = std::log (std::max ((double) src[(size_t) i + 1], 1.0e-20));
            out[(size_t) k] = (float) (std::exp (a + fr * (b - a)) * scale);
        }
        return out;
    }

    double delayedL() const noexcept { return dryL; }
    double delayedR() const noexcept { return dryR; }
    // display (benign races: read by the editor)
    float inDb[kDisplay] = {}, noiseDb[kDisplay] = {}, gainDb[kDisplay] = {};
    std::atomic<float> speechAmt { 0.0f };

    void processFrame (double& l, double& r) noexcept
    {
        const size_t wi = (size_t) w;
        dryL = inL[wi]; dryR = inR[wi];
        const double yl = accL[wi], yr = accR[wi];
        accL[wi] = accR[wi] = 0.0;
        inL[wi] = l; inR[wi] = r;
        w = (w + 1) % N;
        if (++count >= H) { count = 0; frame(); }
        if (settings.delta) { l = dryL - yl; r = dryR - yr; }
        else { l = yl; r = yr; }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static double sstep (double x) noexcept { x = std::clamp (x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }

    void fft (bool inverse) noexcept
    {
        for (int i = 0; i < N; ++i) { const int j = rev[(size_t) i]; if (j > i) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); } }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const auto t = tw[(size_t) (k * step)];
                    const double wr = t.real(), wi = inverse ? -t.imag() : t.imag();
                    const size_t a = (size_t) (i + k), b = (size_t) (i + k + half);
                    const double xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                }
        }
    }

    void frame() noexcept
    {
        const double dt = (double) H / sr;
        for (int i = 0; i < N; ++i)
        {
            const size_t src = (size_t) ((w + i) % N);
            re[(size_t) i] = inL[src] * win[(size_t) i]; im[(size_t) i] = inR[src] * win[(size_t) i];
        }
        fft (false);
        if (mtx.try_lock())
        {
            if (havePending) { for (int k = 0; k < K; ++k) prof[(size_t) k] = pending[(size_t) k]; hasProf = true; havePending = false; }
            if (snapDirty) { snapshot.assign (prof.begin(), prof.end()); snapDirty = false; }
            mtx.unlock();
        }
        const double norm = 1.0 / ((double) N * (double) N);
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k, nk = (size_t) ((N - k) % N);
            const std::complex<double> z (re[kk], im[kk]), zc = std::conj (std::complex<double> (re[nk], im[nk]));
            const std::complex<double> xl = 0.5 * (z + zc), xr = std::complex<double> (0.0, -0.5) * (z - zc);
            P[kk] = 0.5 * (std::norm (xl) + std::norm (xr)) * norm * 4.0;
        }
        const bool first = frames == 0;
        ++frames;

        // ---- learning: the profile is the mean power of the passage
        if (learning)
        {
            ++learnCount;
            for (int k = 0; k < K; ++k) profLive[(size_t) k] += (P[(size_t) k] - profLive[(size_t) k]) / (double) learnCount;
            if (learnCount >= 4)
            {
                prof = profLive; hasProf = true;
                if (mtx.try_lock()) { snapshot.assign (prof.begin(), prof.end()); mtx.unlock(); } else snapDirty = true;
            }
        }

        // ---- adaptive floor: minimum of the lightly smoothed power over the last ~2.4 s (8 sub-windows of 0.3 s).
        // A window instead of "falls at once, rises slowly": digital silence, a fade-in or a gate at the start can not poison the floor for seconds.
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k;
            Ps[kk] = first ? P[kk] : Ps[kk] + 0.35 * (P[kk] - Ps[kk]);
        }
        double frameE = 0.0;
        for (int k = 4; k < K; ++k) frameE += Ps[(size_t) k];
        const bool live = frames > N / H + 2 && frameE > 1.0e-12 * (double) K;                              // ignore digital silence: it says nothing about the noise
        if (live)
        {
            const bool fresh = ! floorValid;
            for (int k = 0; k < K; ++k)
            {
                const size_t kk = (size_t) k;
                if (fresh) { curMin[kk] = Ps[kk]; for (int m = 0; m < kSub; ++m) subMin[(size_t) m * (size_t) K + kk] = Ps[kk]; }
                else curMin[kk] = std::min (curMin[kk], Ps[kk]);
            }
            floorValid = true;
            if (++subCount >= subLen)
            {
                subCount = 0; subPos = (subPos + 1) % kSub;
                for (int k = 0; k < K; ++k) { subMin[(size_t) subPos * (size_t) K + (size_t) k] = curMin[(size_t) k]; curMin[(size_t) k] = Ps[(size_t) k]; }
            }
            for (int k = 0; k < K; ++k)
            {
                double m = curMin[(size_t) k];
                for (int j = 0; j < kSub; ++j) m = std::min (m, subMin[(size_t) j * (size_t) K + (size_t) k]);
                Pa[(size_t) k] = m;
            }
        }
        else if (! floorValid) for (int k = 0; k < K; ++k) Pa[(size_t) k] = std::max (Ps[(size_t) k], 1.0e-18);

        // ---- noise estimate
        const double bias = kFloorBias;                                                // a minimum follower sits below the mean
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k;
            const double fk = (double) k * sr / (double) N;
            const double bk = bias;
            double n = Pa[kk] * bk;
            if (hasProf) n = settings.adaptive ? std::clamp (n, 0.7 * prof[kk], 4.0 * prof[kk]) : prof[kk];
            Pn[kk] = n;
        }
        // smooth across frequency (power, +-2 bins)
        for (int k = 0; k < K; ++k)
        {
            double s = 0.0; int c = 0;
            for (int j = std::max (0, k - 2); j <= std::min (K - 1, k + 2); ++j) { s += Pn[(size_t) j]; ++c; }
            Gtmp[(size_t) k] = s / c;
        }
        // ---- room: late reverberation from 60 ms earlier
        const double T60 = 0.5, decay = std::exp (-2.0 * 6.9078 * (histLen * dt) / T60);
        const double roomAmt = settings.room * 1.0;
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k;
            const double past = hist[(size_t) histPos * (size_t) K + kk];
            lateP[kk] = roomAmt * decay * past;
            hist[(size_t) histPos * (size_t) K + kk] = Ps[kk];
        }
        histPos = (histPos + 1) % histLen;

        // ---- gain
        const double beta = 0.6 + 2.4 * settings.sensitivity;
        const double gMin = std::pow (10.0, -settings.reductionDb / 20.0);
        const double alpha = 0.90 + 0.07 * settings.smooth;
        const double fBin = sr / (double) N;
        const double q = 0.55 + 0.35 * settings.sensitivity;
        double snrBb = 0.0, snrHf = 0.0; int nBb = 0, nHf = 0;
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k;
            const double n = std::max (Gtmp[kk] + lateP[kk], 1.0e-18);
            const double gamma = P[kk] / n;
            const double xi = std::max (alpha * prevSnrPow[kk] + (1.0 - alpha) * std::max (gamma - 1.0, 0.0), 1.0e-3);
            const double nu = gamma * xi / (1.0 + xi);
            const double pAbs = q / (1.0 - q);
            const double pk = 1.0 / (1.0 + pAbs * (1.0 + xi) * std::exp (-std::min (nu, 50.0)));        // probability that speech is in this bin
            pr[kk] = pr[kk] + 0.5 * (pk - pr[kk]);
            Gtmp2[kk] = xi / (xi + beta);
            const double f = k * fBin;
            if (f >= 200.0 && f <= 4000.0) { snrBb += std::max (gamma - 1.0, 0.0); ++nBb; }
            if (f >= 4000.0 && f <= 10000.0) { snrHf += std::max (gamma - 1.0, 0.0); ++nHf; }
        }
        for (int k = 0; k < K; ++k)
        {
            const double pm = (pr[(size_t) std::max (0, k - 1)] + 2.0 * pr[(size_t) k] + pr[(size_t) std::min (K - 1, k + 1)]) * 0.25;
            const double g1 = std::clamp (Gtmp2[(size_t) k], gMin, 1.0);
            G[(size_t) k] = std::clamp (std::exp (pm * std::log (g1) + (1.0 - pm) * std::log (gMin)), gMin, 1.0);
        }
        // speech presence: broadband and 4-10 kHz (sibilants)
        const double pBb = sstep ((10.0 * std::log10 (1.0 + snrBb / std::max (1, nBb)) - 3.0) / 6.0);
        const double pHf = sstep ((10.0 * std::log10 (1.0 + snrHf / std::max (1, nHf)) - 4.0) / 6.0);
        speech += (pBb > speech ? 0.8 : 0.25) * (pBb - speech);
        speechHf += (pHf > speechHf ? 0.8 : 0.25) * (pHf - speechHf);
        speechAmt.store ((float) speech, std::memory_order_relaxed);

        // ---- Air: less reduction at the top while the voice is there
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k;
            const double f = k * fBin;
            const double h = std::max (0.55 * sstep ((f - 700.0) / 1000.0), sstep ((f - 2500.0) / 5000.0));   // from 700 Hz at about half strength, full from 2.5 kHz
            const double p = std::max (speech, speechHf * sstep ((f - 3500.0) / 1500.0));
            const double scale = 1.0 - 0.8 * settings.air * h * p;
            G[kk] = std::max (gMin, std::exp (std::log (std::max (G[kk], 1.0e-9)) * scale));
        }
        // ---- Body: the same for the low harmonics of a deep voice
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k;
            const double f = k * fBin;
            const double w = sstep ((f - 48.0) / 28.0) * (1.0 - sstep ((f - 350.0) / 250.0));          // full from ~76 Hz: covers the fundamental of a very deep voice
            if (w <= 0.0) continue;
            const double scale = 1.0 - 1.0 * settings.body * w * speech;
            G[kk] = std::max (gMin, std::exp (std::log (std::max (G[kk], 1.0e-9)) * scale));
        }
        // ---- Low cut: rumble below the cut goes away whatever the voice does (steep, zero phase)
        if (settings.lowCut > 1.0)
        {
            for (int k = 1; k < K; ++k)
            {
                const double f = k * fBin;
                if (f >= settings.lowCut * 1.6) break;
                const double x = f / settings.lowCut;
                const double hpg = x >= 1.0 ? 1.0 : std::pow (x, 12.0);
                G[(size_t) k] = std::min (G[(size_t) k], std::max (1.0e-3, hpg));
            }
            G[0] = 1.0e-3;
        }
        // decision-directed memory uses the gain that was really applied
        // smooth across bins, then slower closing than opening
        for (int k = 0; k < K; ++k)
        {
            const double a = G[(size_t) std::max (0, k - 1)], b = G[(size_t) k], c = G[(size_t) std::min (K - 1, k + 1)];
            Gtmp[(size_t) k] = 0.25 * a + 0.5 * b + 0.25 * c;
        }
        const double closeC = 0.85 - 0.6 * settings.smooth;
        for (int k = 0; k < K; ++k)
        {
            const size_t kk = (size_t) k;
            const double g = Gtmp[kk];
            Gs[kk] += (g > Gs[kk] ? 1.0 : closeC) * (g - Gs[kk]);
            prevSnrPow[kk] = Gs[kk] * Gs[kk] * P[kk] / std::max (Pn[kk] + lateP[kk], 1.0e-18);
        }
        const bool pass = learning || ! settings.enabled;
        for (int k = 0; k < K; ++k)
        {
            const double g = pass ? 1.0 : Gs[(size_t) k];
            re[(size_t) k] *= g; im[(size_t) k] *= g;
            if (k > 0 && k < N / 2) { re[(size_t) (N - k)] *= g; im[(size_t) (N - k)] *= g; }
        }
        for (int d = 0; d < kDisplay; ++d)
        {
            double sp = 0, nz = 0, mg = 1.0;
            for (int k = dispLo[d]; k <= dispHi[d]; ++k) { sp = std::max (sp, P[(size_t) k]); nz = std::max (nz, Pn[(size_t) k]); mg = std::min (mg, Gs[(size_t) k]); }
            inDb[d] = (float) (10.0 * std::log10 (sp + 1.0e-14)); noiseDb[d] = (float) (10.0 * std::log10 (nz + 1.0e-14)); gainDb[d] = (float) (20.0 * std::log10 (mg + 1.0e-6));
        }

        fft (true);
        const double scale = 0.5 / (double) N;
        for (int i = 0; i < N; ++i)
        {
            const size_t dst = (size_t) ((w + i) % N);
            accL[dst] += re[(size_t) i] * win[(size_t) i] * scale; accR[dst] += im[(size_t) i] * win[(size_t) i] * scale;
        }
    }

    double sr = 48000.0, dryL = 0.0, dryR = 0.0, speech = 0.0, speechHf = 0.0;
    static constexpr double kFloorBias = 2.8;
    static constexpr int kSub = 8;
    int subLen = 30, subPos = 0, subCount = 0;
    bool floorValid = false;
    int N = 2048, H = 512, K = 1025, w = 0, count = 0, frames = 0, histLen = 6, histPos = 0, learnCount = 0;
    bool hasProf = false, learning = false, snapDirty = false, havePending = false;
    std::vector<double> scratch, inL, inR, accL, accR, re, im, win, P, Ps, Pa, Pn, G, Gs, Gtmp, prevSnrPow, prof, profLive, lateP, hist, pr, Gtmp2, subMin, curMin;
    std::vector<std::complex<double>> tw;
    std::vector<int> rev;
    int dispLo[kDisplay] = {}, dispHi[kDisplay] = {};
    mutable std::mutex mtx;
    std::vector<float> snapshot, pending;
    Settings settings;
};
} // namespace gf
