/*
  ==============================================================================
    DaliDist — Analog Color Engine
    Dali Audio

    Pure C++ (no JUCE dependency) so the engine can be unit-tested on its own.

    The engine runs at the OVERSAMPLED rate and outputs ONLY the "color"
    (the difference between the analog-modelled signal and the clean input).
    The processor adds this color to a sample-exact, latency-aligned copy of
    the dry signal. Consequences:
      * Drive = 0  ->  color = 0  ->  output is bit-identical to the input.
      * The clean path never passes through filters, so parallel mix
        can never comb-filter or lose quality.

    Per band:  color_b = LPF_antiHarsh( deEmph( shape(g * preEmph(x_b)) / g ) - x_b )
    Small-signal slope of every shaper is exactly 1, so quiet material
    receives almost no color and loud material receives more (program dependent).
  ==============================================================================
*/
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dali
{
constexpr float kPi = 3.14159265358979323846f;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float smoothstep (float e0, float e1, float x) noexcept
{
    const float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

//==============================================================================
// Filters
//==============================================================================

/** Topology-preserving one-pole (Zavalishin). */
struct OnePole
{
    float g = 0.0f, s = 0.0f;

    void setCutoff (float fc, float fs) noexcept
    {
        const float w = std::tan (kPi * std::min (fc, 0.49f * fs) / fs);
        g = w / (1.0f + w);
    }
    void reset() noexcept { s = 0.0f; }

    inline float lp (float x) noexcept
    {
        const float v = (x - s) * g;
        const float y = v + s;
        s = y + v;
        return y;
    }
    inline float hp (float x) noexcept { return x - lp (x); }
};

/** Simper / Cytomic trapezoidal SVF. */
struct SVF
{
    float a1 = 1, a2 = 0, a3 = 0, k = 1.41421356f;
    float ic1 = 0, ic2 = 0;
    float lpOut = 0, hpOut = 0;

    void set (float fc, float fs, float q) noexcept
    {
        const float g = std::tan (kPi * std::min (fc, 0.49f * fs) / fs);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    inline void tick (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lpOut = v2;
        hpOut = v0 - k * v1 - v2;
    }
};

/** First-order shelf H(s) = K (s + wz) / (s + wp), bilinear with prewarp.
    K = sqrt(wp/wz) -> tilt around the centre (DC and HF gains reciprocal).
    makeInverse() builds the exact inverse, so pre/de-emphasis cancel perfectly. */
struct Shelf1
{
    float b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;

    void design (float fz, float fp, float fs, bool inverse) noexcept
    {
        const float c  = 2.0f * fs;
        float wz = c * std::tan (kPi * std::min (fz, 0.45f * fs) / fs);
        float wp = c * std::tan (kPi * std::min (fp, 0.45f * fs) / fs);
        float K  = std::sqrt (wp / wz);
        if (inverse) { std::swap (wz, wp); K = 1.0f / K; }
        const float n = 1.0f / (c + wp);
        b0 = K * (c + wz) * n;
        b1 = K * (wz - c) * n;
        a1 = (wp - c) * n;
    }
    void reset() noexcept { x1 = y1 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x; y1 = y;
        return y;
    }
};

//==============================================================================
// Deterministic RNG + analog drift source
//==============================================================================
struct XorShift
{
    uint32_t s = 0x9E3779B9u;
    explicit XorShift (uint32_t seed = 1) : s (seed ? seed : 1u) {}
    float next() noexcept   // [-1, 1]
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s & 0xFFFFFF) / (float) 0x7FFFFF - 1.0f;
    }
};

/** Very slow, smooth random walk (≈0.05–0.5 Hz). Deterministic, so bounces are repeatable. */
struct DriftSource
{
    XorShift rng { 1 };
    float target = 0, s1 = 0, s2 = 0, coef = 0;
    int countdown = 0, minHold = 1, maxHold = 1;

    void prepare (float controlRate, uint32_t seed) noexcept
    {
        rng = XorShift (seed);
        coef = 1.0f - std::exp (-1.0f / (1.2f * controlRate));  // ~1.2 s smoothing
        minHold = (int) (1.5f * controlRate);
        maxHold = (int) (5.0f * controlRate);
        target = rng.next(); s1 = s2 = target * 0.5f;
        countdown = minHold;
    }
    float tick() noexcept
    {
        if (--countdown <= 0)
        {
            target = rng.next();
            countdown = minHold + (int) ((rng.next() * 0.5f + 0.5f) * (float) (maxHold - minHold));
        }
        s1 += coef * (target - s1);
        s2 += coef * (s1 - s2);
        return s2;
    }
};

//==============================================================================
// Character modes
//==============================================================================
enum class Mode : int { Tube = 0, Transformer, Tape, Console, Acid, NumModes };

/** Everything that makes a mode a different *circuit*, not just a different EQ. */
struct Voicing
{
    // Transfer-curve family weights (tanh = tube-like, alg = soft/dense, p4 = clean-then-knee)
    float wTanh, wAlg, wP4;
    float driveScale;   // how hard this circuit is hit for the same Drive
    float bias0;        // static asymmetry -> even harmonics
    float biasDyn;      // level-dependent asymmetry (bias shift with level)
    float hyst;         // magnetic-style hysteresis loop width
    float mem;          // slow bias memory (grid-current-like recovery)
    float resLP;        // anti-harshness cutoff for generated harmonics (Hz)
    float preFz, preFp; // pre/de-emphasis shelf (fz == fp -> flat)
    float exc;          // smooth even-order "excitement" on upper band
    float bandLow, bandMid, bandHigh;
    float sag;          // program-dependent soft compression
    float link;         // stereo-linked envelope (console glue)
};

inline Voicing voicingFor (Mode m) noexcept
{
    switch (m)
    {
        //                        wT    wA    wP4   drv   b0    bDyn  hyst  mem   resLP   preFz  preFp  exc   bL    bM    bH    sag   link
        case Mode::Tube:        return { 1.0f, 0.0f, 0.0f, 1.00f, 0.20f, 0.30f, 0.04f, 0.10f,  9000,  1000,  1000, 0.00f, 0.55f, 1.00f, 0.70f, 0.10f, 0.00f };
        case Mode::Transformer: return { 0.0f, 1.0f, 0.0f, 1.15f, 0.06f, 0.06f, 0.10f, 0.04f,  6500,   320,   110, 0.00f, 0.75f, 1.00f, 0.45f, 0.22f, 0.20f };
        case Mode::Tape:        return { 1.0f, 0.0f, 0.0f, 1.80f, 0.03f, 0.03f, 0.14f, 0.03f, 10000,  2400,  9000, 0.00f, 0.60f, 1.00f, 0.90f, 0.16f, 0.10f };
        case Mode::Console:     return { 0.0f, 0.0f, 1.0f, 1.00f, 0.04f, 0.02f, 0.02f, 0.00f, 11000,  1000,  1000, 0.00f, 0.50f, 1.00f, 1.00f, 0.06f, 0.60f };
        case Mode::Acid:        return { 1.0f, 0.0f, 0.0f, 1.10f, 0.14f, 0.35f, 0.05f, 0.06f, 10000,   800,  3200, 0.35f, 0.35f, 1.20f, 1.10f, 0.08f, 0.00f };
        default: break;
    }
    return voicingFor (Mode::Tube);
}

//==============================================================================
// Engine parameters (targets; the engine smooths them internally)
//==============================================================================
struct EngineParams
{
    float drive     = 0.15f;  // 0..1
    float character = 0.35f;  // 0 Warm .. 1 Excited
    Mode  mode      = Mode::Tube;
    float low       = 0.25f;  // 0..1.5 band color amounts
    float mid       = 1.00f;
    float high      = 0.60f;
    float xLow      = 150.0f; // Hz
    float xHigh     = 3500.0f;
    float drift     = 0.30f;  // 0..1
};

//==============================================================================
// Engine
//==============================================================================
class ColorEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kControlInterval = 16;   // samples at oversampled rate

    void prepare (double oversampledRate, int numChannels)
    {
        fs = (float) oversampledRate;
        numCh = std::clamp (numChannels, 1, kMaxChannels);
        controlRate = fs / (float) kControlInterval;
        ctrlCoef = 1.0f - std::exp (-1.0f / (0.030f * controlRate)); // ~30 ms param smoothing

        envAtk  = 1.0f - std::exp (-1.0f / (0.008f * fs));
        envRel  = 1.0f - std::exp (-1.0f / (0.120f * fs));
        memCoef = 1.0f - std::exp (-1.0f / (0.150f * fs));

        for (int c = 0; c < kMaxChannels; ++c)
        {
            auto& ch = chans[(size_t) c];
            for (int d = 0; d < 3; ++d)
                ch.drift[(size_t) d].prepare (controlRate, 0xDA11u + 7919u * (uint32_t) (c * 3 + d));
            XorShift tol (0xA0D10u + 104729u * (uint32_t) c);   // fixed "component tolerances"
            ch.tolGain = tol.next(); ch.tolBias = tol.next(); ch.tolCut = tol.next();
            for (auto& b : ch.band)
            {
                b.playLP.setCutoff (6000.0f, fs);
                b.dc.setCutoff (8.0f, fs);
                b.exHP.setCutoff (1800.0f, fs);
            }
        }
        snapSmoothers();
        reset();
    }

    void reset()
    {
        for (auto& ch : chans)
        {
            ch.x1a.reset(); ch.x1b.reset(); ch.x2a.reset(); ch.x2b.reset();
            for (auto& b : ch.band)
            {
                b.env = 0; b.play = 0; b.biasMem = 0;
                b.playLP.reset(); b.dc.reset(); b.exHP.reset();
                b.pre.reset(); b.de.reset(); b.res.reset();
            }
        }
        ctrlCounter = 0;
        colorPow = 0; inPow = 0;
    }

    void setParams (const EngineParams& p) noexcept { target = p; }

    /** Call once after prepare() + setParams() to start without a parameter glide. */
    void snapSmoothers() noexcept
    {
        cur.drive = target.drive; cur.character = target.character; cur.drift = target.drift;
        cur.band[0] = target.low; cur.band[1] = target.mid; cur.band[2] = target.high;
        cur.logXLow = std::log (target.xLow); cur.logXHigh = std::log (target.xHigh);
        cur.v = voicingFor (target.mode);
        cur.logPreFz = std::log (cur.v.preFz); cur.logPreFp = std::log (cur.v.preFp);
    }

    /** In place: input = oversampled dry signal, output = color only. */
    void process (float* const* data, int numChannels, int numSamples) noexcept
    {
        const int nc = std::min (numChannels, numCh);
        for (int start = 0; start < numSamples;)
        {
            if (ctrlCounter <= 0) { updateControl (nc); ctrlCounter = kControlInterval; }
            const int todo = std::min (ctrlCounter, numSamples - start);

            for (int c = 0; c < nc; ++c)
            {
                auto& ch = chans[(size_t) c];
                float* d = data[c] + start;
                for (int i = 0; i < todo; ++i)
                {
                    const float x = d[i];

                    // Band split. Subtractive, so low + mid + high == x exactly.
                    ch.x1a.tick (x);           ch.x1b.tick (ch.x1a.hpOut);
                    const float rest = ch.x1b.hpOut;               // LR4 high-pass
                    const float low  = x - rest;
                    ch.x2a.tick (rest);        ch.x2b.tick (ch.x2a.hpOut);
                    const float high = ch.x2b.hpOut;
                    const float mid  = rest - high;

                    float color = 0.0f;
                    if (ch.band[0].amt > 1.0e-6f) color += processBand (low,  ch.band[0]);
                    if (ch.band[1].amt > 1.0e-6f) color += processBand (mid,  ch.band[1]);
                    if (ch.band[2].amt > 1.0e-6f) color += processBand (high, ch.band[2]);

                    inPow    += x * x;
                    colorPow += color * color;
                    d[i] = color;
                }
            }
            ctrlCounter -= todo;
            start += todo;
        }
    }

    /** Harmonic-color energy relative to input since last call (for the UI "color" meter). */
    float takeColorRatio() noexcept
    {
        const float r = inPow > 1.0e-9f ? std::sqrt (colorPow / inPow) : 0.0f;
        colorPow = 0; inPow = 0;
        return r;
    }

private:
    struct Band
    {
        // audio-rate state
        float env = 0, play = 0, biasMem = 0;
        OnePole playLP, dc, exHP;
        Shelf1 pre, de;
        SVF res;
        // control-rate derived
        float amt = 0;         // band color amount (includes drive color scale)
        float g = 1;           // shaper gain
        float sag = 0, hyst = 0, mem = 0, exc = 0;
        float bias = 0;
        float wT = 1, wA = 0, wP = 0;
        float sT = 0, nT = 1, sA = 0, nA = 1, sP = 0, nP = 1;   // s(b) and 1/s'(b)
        bool  usePre = false;
    };

    struct Channel
    {
        SVF x1a, x1b, x2a, x2b;
        std::array<Band, 3> band;
        std::array<DriftSource, 3> drift;
        float tolGain = 0, tolBias = 0, tolCut = 0;
    };

    struct Current
    {
        float drive = 0, character = 0, drift = 0;
        float band[3] {};
        float logXLow = 0, logXHigh = 0, logPreFz = 0, logPreFp = 0;
        Voicing v {};
    };

    //--------------------------------------------------------------------------
    static inline float tanhS (float v) noexcept { return std::tanh (v); }
    static inline float algS  (float v) noexcept { return v / std::sqrt (1.0f + v * v); }
    static inline float p4S   (float v) noexcept { const float v2 = v * v; return v / std::sqrt (std::sqrt (1.0f + v2 * v2)); }

    inline float shape (const Band& b, float v) noexcept
    {
        const float vb = v + b.bias;
        float y = 0.0f;
        if (b.wT > 1.0e-4f) y += b.wT * (tanhS (vb) - b.sT) * b.nT;
        if (b.wA > 1.0e-4f) y += b.wA * (algS  (vb) - b.sA) * b.nA;
        if (b.wP > 1.0e-4f) y += b.wP * (p4S   (vb) - b.sP) * b.nP;
        return y;
    }

    inline float processBand (float xb, Band& b) noexcept
    {
        // Program-dependent envelope (mean square)
        const float sq = xb * xb;
        b.env += (sq > b.env ? envAtk : envRel) * (sq - b.env);
        const float lvl  = std::sqrt (b.env) * b.g;
        const float gEff = b.g / (1.0f + b.sag * lvl);          // soft sag -> natural compression

        const float xp = b.usePre ? b.pre.process (xb) : xb;
        const float u  = gEff * xp;

        // Hysteresis: rate-independent play operator, only active near the knee
        constexpr float W = 0.25f;
        const float dlt = u - b.play;
        if (dlt >  W) b.play = u - W;
        else if (dlt < -W) b.play = u + W;
        const float lag  = b.playLP.lp (b.play) - u;
        const float knee = (u * u) / (1.0f + u * u);
        const float v    = u + b.hyst * knee * lag;

        // Slow bias memory: positive excursions shift the operating point, recovering over ~150 ms
        const float pos = v > 0.0f ? v / (1.0f + v) : 0.0f;              // bounded, like real grid current
        b.biasMem += memCoef * (-b.mem * pos - b.biasMem);

        float y = shape (b, v) / b.g;                            // divide by g (not gEff): sag = compression
        if (b.usePre) y = b.de.process (y);

        float r = y - xb;                                        // color only

        if (b.exc > 1.0e-4f)                                     // smooth even-order excitement
        {
            const float e = b.exHP.hp (xb);
            r += b.exc * e * std::tanh (3.0f * b.g * e);
        }

        b.res.tick (r);                                          // anti-harshness on generated harmonics
        r = b.res.lpOut;
        r -= b.dc.lp (r);                                        // DC only removed from color, never from dry
        return r * b.amt;
    }

    //--------------------------------------------------------------------------
    template <typename T> inline void approach (T& c, T t) noexcept { c += (t - c) * ctrlCoef; }

    void updateControl (int nc) noexcept
    {
        // --- smooth user params and mode voicing (mode switches glide, no clicks)
        approach (cur.drive, target.drive);
        approach (cur.character, target.character);
        approach (cur.drift, target.drift);
        approach (cur.band[0], target.low);
        approach (cur.band[1], target.mid);
        approach (cur.band[2], target.high);
        approach (cur.logXLow,  std::log (std::clamp (target.xLow, 40.0f, 600.0f)));
        approach (cur.logXHigh, std::log (std::clamp (target.xHigh, 1000.0f, 12000.0f)));

        const Voicing tv = voicingFor (target.mode);
        auto& v = cur.v;
        approach (v.wTanh, tv.wTanh); approach (v.wAlg, tv.wAlg); approach (v.wP4, tv.wP4);
        approach (v.driveScale, tv.driveScale); approach (v.bias0, tv.bias0); approach (v.biasDyn, tv.biasDyn);
        approach (v.hyst, tv.hyst); approach (v.mem, tv.mem); approach (v.resLP, tv.resLP);
        approach (v.exc, tv.exc); approach (v.bandLow, tv.bandLow); approach (v.bandMid, tv.bandMid);
        approach (v.bandHigh, tv.bandHigh); approach (v.sag, tv.sag); approach (v.link, tv.link);
        approach (cur.logPreFz, std::log (tv.preFz));
        approach (cur.logPreFp, std::log (tv.preFp));

        // --- Drive: gentle curve so 5–20% is the detailed, useful region
        const float d = std::clamp (cur.drive, 0.0f, 1.0f);
        const float driveDb    = 2.0f + 26.0f * std::pow (d, 1.6f);
        const float colorScale = std::pow (std::min (1.0f, d / 0.30f), 0.7f);   // 0 at Drive 0 -> exact bypass
        constexpr float kRef   = 1.6f;                                            // internal operating level

        // --- Character: Warm -> Rich -> Open -> Excited
        const float ch = std::clamp (cur.character, 0.0f, 1.0f);
        const float biasMul = lerp (1.6f, 0.55f, ch);                  // warm = more even harmonics
        const float brightMul = 0.6f * std::pow (2.6f, ch);            // harmonics get more open
        const float excAdd = 0.30f * smoothstep (0.55f, 1.0f, ch);     // excited = upper excitement
        const float lowMul = lerp (1.25f, 0.8f, ch);                   // warm = thicker lows
        const float darken = 1.0f - 0.40f * d;                         // more drive -> darker, denser, not harsher

        const float fz = std::exp (cur.logPreFz), fp = std::exp (cur.logPreFp);
        const bool usePre = std::abs (cur.logPreFz - cur.logPreFp) > 0.01f;
        const float xl = std::exp (cur.logXLow);
        const float xh = std::max (std::exp (cur.logXHigh), xl * 3.0f);

        // Stereo-linked envelope (console glue)
        float envMax[3] = { 0, 0, 0 };
        for (int c = 0; c < nc; ++c)
            for (int b = 0; b < 3; ++b)
                envMax[b] = std::max (envMax[b], chans[(size_t) c].band[(size_t) b].env);

        const float bandVoice[3] = { v.bandLow * lowMul, v.bandMid, v.bandHigh };
        const float bandCut[3]   = { 0.45f, 1.0f, 1.15f };   // low-band harmonics stay darkest

        for (int c = 0; c < nc; ++c)
        {
            auto& chn = chans[(size_t) c];
            chn.x1a.set (xl, fs, 0.70710678f); chn.x1b.set (xl, fs, 0.70710678f);
            chn.x2a.set (xh, fs, 0.70710678f); chn.x2b.set (xh, fs, 0.70710678f);

            // Analog drift + fixed L/R component mismatch
            const float da = cur.drift;
            const float dG = chn.drift[0].tick(), dB = chn.drift[1].tick(), dC = chn.drift[2].tick();
            const float gDrift   = dbToGain (da * (0.25f * dG + 0.15f * chn.tolGain));
            const float biasDrift = da * (0.030f * dB + 0.015f * chn.tolBias);
            const float cutDrift  = 1.0f + da * (0.05f * dC + 0.03f * chn.tolCut);
            const float hystDrift = 1.0f + da * 0.2f * dB;

            for (int bi = 0; bi < 3; ++bi)
            {
                auto& b = chn.band[(size_t) bi];
                b.amt = colorScale * cur.band[bi] * bandVoice[bi];
                b.g   = dbToGain (driveDb) * kRef * v.driveScale * gDrift;
                b.sag = v.sag;
                b.hyst = v.hyst * hystDrift;
                b.mem  = v.mem;
                b.exc  = (bi == 0 ? 0.0f : (v.exc + excAdd) * (bi == 2 ? 1.0f : 0.5f));
                b.wT = v.wTanh; b.wA = v.wAlg; b.wP = v.wP4;

                const float env = lerp (b.env, envMax[bi], v.link);
                const float lvl = std::sqrt (env) * b.g;
                b.bias = std::clamp ((v.bias0 + v.biasDyn * lvl / (1.0f + lvl)) * biasMul
                                        + b.biasMem + biasDrift, -0.9f, 0.9f);

                // s(b) and normalisation 1/s'(b) -> small-signal slope exactly 1
                const float bb = b.bias;
                const float t  = std::tanh (bb);
                b.sT = t;              b.nT = 1.0f / (1.0f - t * t);
                b.sA = algS (bb);      b.nA = std::pow (1.0f + bb * bb, 1.5f);
                const float b4 = bb * bb * bb * bb;
                b.sP = p4S (bb);       b.nP = std::pow (1.0f + b4, 1.25f);

                b.usePre = usePre;
                if (usePre) { b.pre.design (fz, fp, fs, false); b.de.design (fz, fp, fs, true); }

                const float fc = std::clamp (v.resLP * brightMul * darken * bandCut[bi] * cutDrift, 1500.0f, 20000.0f);
                b.res.set (fc, fs, 0.6f);
            }
        }
    }

    float fs = 192000.0f, controlRate = 12000.0f, ctrlCoef = 0.1f;
    float envAtk = 0, envRel = 0, memCoef = 0;
    int numCh = 2, ctrlCounter = 0;
    std::array<Channel, kMaxChannels> chans;
    EngineParams target;
    Current cur;
    float colorPow = 0, inPow = 0;
};

} // namespace dali
