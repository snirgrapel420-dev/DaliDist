// g++ -O2 -std=c++17 -I../Source dsp_test.cpp -o dsp_test && ./dsp_test
#include "DaliDistDSP.h"
#include <cstdio>
#include <vector>

using namespace dali;
static const char* modeNames[] = { "TUBE", "TRANSFORMER", "TAPE", "CONSOLE", "ACID" };
constexpr double FS = 192000.0;  // 48k x4 oversampled

// Goertzel magnitude of frequency f over the buffer (Hann-windowed)
static double tone (const std::vector<float>& x, double f)
{
    const size_t n = x.size();
    const double w = 2.0 * M_PI * f / FS, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0, wsum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double h = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (n - 1));
        wsum += h;
        const double s = x[i] * h + c * s1 - s2; s2 = s1; s1 = s;
    }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (wsum * 0.5);
}
static double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// Runs the engine and returns dry + color (what the plugin outputs at 100% wet, pre auto-gain)
static std::vector<float> run (EngineParams p, double freq, double ampDb, double seconds = 1.5)
{
    ColorEngine e; e.setParams (p); e.prepare (FS, 1); e.snapSmoothers();
    const size_t n = (size_t) (seconds * FS);
    std::vector<float> x (n), y (n);
    const double a = std::pow (10.0, ampDb / 20.0);
    for (size_t i = 0; i < n; ++i) x[i] = (float) (a * std::sin (2.0 * M_PI * freq * (double) i / FS));
    y = x;
    float* ch[1] = { y.data() };
    e.process (ch, 1, (int) n);
    for (size_t i = 0; i < n; ++i) y[i] += x[i];
    return std::vector<float> (y.begin() + (long) (n / 3), y.end());    // skip settling
}

int main()
{
    // 1) NULL TEST: Drive 0 must be bit-exact bypass
    {
        EngineParams p; p.drive = 0.0f;
        ColorEngine e; e.setParams (p); e.prepare (FS, 2); e.snapSmoothers();
        std::vector<float> L (48000), R (48000);
        XorShift r (7); for (size_t i = 0; i < L.size(); ++i) { L[i] = r.next(); R[i] = r.next(); }
        float* ch[2] = { L.data(), R.data() };
        e.process (ch, 2, (int) L.size());
        double mx = 0; for (size_t i = 0; i < L.size(); ++i) mx = std::max ({ mx, (double) std::abs (L[i]), (double) std::abs (R[i]) });
        std::printf ("[null] drive 0 -> max color = %g  %s\n\n", mx, mx == 0.0 ? "PASS (bit-exact)" : "FAIL");
    }

    // 2) HARMONIC PROFILE: 220 Hz @ -12 dBFS, Character 0.35
    std::printf ("[harmonics] 220 Hz @ -12 dBFS. H2..H5 in dB relative to fundamental, fund change in dB\n");
    for (int m = 0; m < 5; ++m)
    {
        std::printf ("%s\n", modeNames[m]);
        for (float d : { 0.05f, 0.10f, 0.20f, 0.40f, 0.70f, 1.00f })
        {
            EngineParams p; p.mode = (Mode) m; p.drive = d; p.drift = 0;
            p.low = 1.0f; p.mid = 1.0f; p.high = 1.0f;   // 220 Hz sits in the mid band
            auto y = run (p, 220.0, -12.0);
            const double f = tone (y, 220.0);
            std::printf ("  drive %3.0f%%  fund %+5.2f  H2 %6.1f  H3 %6.1f  H4 %6.1f  H5 %6.1f\n",
                         d * 100, db (f) - (-12.0),
                         db (tone (y, 440) / f), db (tone (y, 660) / f), db (tone (y, 880) / f), db (tone (y, 1100) / f));
        }
    }

    // 3) PROGRAM DEPENDENCE: same drive, different input levels
    std::printf ("\n[level dependence] TUBE drive 15%%, H2/H3 vs input level\n");
    for (double lv : { -36.0, -24.0, -12.0, -6.0, 0.0 })
    {
        EngineParams p; p.drive = 0.15f; p.drift = 0; p.mid = 1.0f;
        auto y = run (p, 220.0, lv);
        const double f = tone (y, 220.0);
        std::printf ("  in %+5.0f dBFS  H2 %6.1f  H3 %6.1f\n", lv, db (tone (y, 440) / f), db (tone (y, 660) / f));
    }

    // 4) BASS PROTECTION: 50 Hz sub, default band settings
    std::printf ("\n[bass] 50 Hz @ -6 dBFS, default bands, H2/H3 at drive 20%% / 60%%\n");
    for (float d : { 0.2f, 0.6f })
    {
        EngineParams p; p.drive = d; p.drift = 0;
        auto y = run (p, 50.0, -6.0, 2.0);
        const double f = tone (y, 50.0);
        std::printf ("  drive %2.0f%%  H2 %6.1f  H3 %6.1f\n", d * 100, db (tone (y, 100) / f), db (tone (y, 150) / f));
    }

    // 5) ANTI-HARSHNESS: 2.5 kHz lead tone, energy of harmonics above 9 kHz
    std::printf ("\n[harshness] 2.5 kHz @ -10 dBFS, level of H4..H8 (10-20 kHz) re fundamental\n");
    for (int m = 0; m < 5; ++m)
    {
        std::printf ("  %-12s", modeNames[m]);
        for (float d : { 0.2f, 0.5f, 1.0f })
        {
            EngineParams p; p.mode = (Mode) m; p.drive = d; p.drift = 0; p.high = 1.0f; p.mid = 1.0f;
            auto y = run (p, 2500.0, -10.0, 0.6);
            const double f = tone (y, 2500);
            double e = 0; for (int h = 4; h <= 8; ++h) { const double t = tone (y, 2500.0 * h); e += t * t; }
            std::printf ("  d%3.0f%%: %6.1f dB", d * 100, db (std::sqrt (e) / f));
        }
        std::printf ("\n");
    }

    // 6) STABILITY: hot noise + mode switching + parameter jumps
    {
        ColorEngine e; EngineParams p; p.drive = 1.0f; p.drift = 1.0f; p.character = 1.0f;
        e.setParams (p); e.prepare (FS, 2); e.snapSmoothers();
        XorShift r (3); bool ok = true; double mx = 0;
        std::vector<float> L (4096), R (4096);
        for (int blk = 0; blk < 400; ++blk)
        {
            p.mode = (Mode) (blk / 20 % 5); p.drive = (blk % 7) / 6.0f; p.xLow = 60 + (blk % 5) * 80; e.setParams (p);
            for (size_t i = 0; i < L.size(); ++i) { L[i] = 4.0f * r.next(); R[i] = (i % 97 == 0) ? 8.0f : 0.0f; }
            float* ch[2] = { L.data(), R.data() };
            e.process (ch, 2, (int) L.size());
            for (size_t i = 0; i < L.size(); ++i)
            {
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) ok = false;
                mx = std::max ({ mx, (double) std::abs (L[i]), (double) std::abs (R[i]) });
            }
        }
        std::printf ("\n[stability] +12 dBFS noise/impulses, mode+param switching: %s (max |color| %.2f)\n", ok ? "PASS" : "FAIL (NaN/Inf)", mx);
    }
    return 0;
}
