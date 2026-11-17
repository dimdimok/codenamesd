#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include <cmath>
#include <memory>

// (comment)
// (comment)
class BeatTracker
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hop = (fs > 66000.0) ? 1024 : 512;
        fftSize = hop * 2;
        fft = std::make_unique<juce::dsp::FFT> (hop == 512 ? 10 : 11);

        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);

        frameRate = (float) (fs / hop);
        envLen = (int) (frameRate * 8.0f); // (comment)

        frame.assign ((size_t) fftSize, 0.0f);
        fftData.assign ((size_t) fftSize * 2, 0.0f);
        prevMag.assign ((size_t) fftSize / 2 + 1, 0.0f);
        env.assign ((size_t) envLen, 0.0f);
        lin.assign ((size_t) envLen, 0.0f);
        ac.assign ((size_t) envLen, 0.0f);
        pref.assign ((size_t) envLen + 1, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        std::fill (prevMag.begin(), prevMag.end(), 0.0f);
        std::fill (env.begin(), env.end(), 0.0f);
        hopFill = 0; envWrite = 0; envCount = 0; sinceAnalysis = 0;
        energyAcc = 0.0f; level = 0.0f;
        bpm = 0.0f; cand = 0.0f; candCount = 0;
        bpmOut = 0.0f; confOut = 0.0f;
    }

    void setRange (float minB, float maxB) { minBpm = minB; maxBpm = juce::jmax (maxB, minB + 10.0f); }

    void pushSample (float x)
    {
        frame[(size_t) (hop + hopFill)] = x;
        energyAcc += x * x;
        if (++hopFill >= hop)
        {
            hopFill = 0;
            onFrame();
        }
    }

    float getBpm() const        { return bpmOut.load(); } // (comment)
    float getConfidence() const { return confOut.load(); }

private:
    void onFrame()
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = frame[(size_t) i] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

        fft->performFrequencyOnlyForwardTransform (fftData.data());

        const int maxBin = juce::jmin (fftSize / 2, (int) (10000.0 * fftSize / fs));
        float flux = 0.0f;
        for (int k = 1; k <= maxBin; ++k)
        {
            const float m = std::log1p (fftData[(size_t) k] * 1000.0f / (float) fftSize);
            const float d = m - prevMag[(size_t) k];
            if (d > 0.0f) flux += d;
            prevMag[(size_t) k] = m;
        }
        flux /= (float) maxBin;

        env[(size_t) envWrite] = flux;
        envWrite = (envWrite + 1) % envLen;
        envCount = juce::jmin (envCount + 1, envLen);

        const float rms = std::sqrt (energyAcc / (float) hop);
        energyAcc = 0.0f;
        level += 0.2f * (rms - level);

        if (++sinceAnalysis >= (int) (frameRate * 0.5f))
        {
            sinceAnalysis = 0;
            analyse();
        }

// (comment)
        std::copy (frame.begin() + hop, frame.end(), frame.begin());
    }

    static float parabolic (const std::vector<float>& a, int i)
    {
        if (i <= 1) return (float) i;
        const float y0 = a[(size_t) i - 1], y1 = a[(size_t) i], y2 = a[(size_t) i + 1];
        const float den = y0 - 2.0f * y1 + y2;
        if (std::abs (den) < 1e-9f) return (float) i;
        return (float) i + juce::jlimit (-1.0f, 1.0f, 0.5f * (y0 - y2) / den);
    }

    static bool near (float a, float b, float tol) { return b > 0.0f && std::abs (a / b - 1.0f) < tol; }

    void analyse()
    {
        const int N = envCount;
        if (N < (int) (frameRate * 4.0f)) return; // (comment)
        if (level < 0.003f) return; // (comment)

// (comment)
        const int start = (envWrite - N + envLen) % envLen;
        for (int i = 0; i < N; ++i)
            lin[(size_t) i] = env[(size_t) ((start + i) % envLen)];

// (comment)
        pref[0] = 0.0f;
        for (int i = 0; i < N; ++i) pref[(size_t) i + 1] = pref[(size_t) i] + lin[(size_t) i];
        const int w = juce::jmax (2, (int) (frameRate * 0.4f));
        for (int i = 0; i < N; ++i)
        {
            const int a = juce::jmax (0, i - w), b = juce::jmin (N, i + w + 1);
            const float mean = (pref[(size_t) b] - pref[(size_t) a]) / (float) (b - a);
            lin[(size_t) i] = juce::jmax (0.0f, lin[(size_t) i] - mean);
        }

        float ac0 = 0.0f;
        for (int i = 0; i < N; ++i) ac0 += lin[(size_t) i] * lin[(size_t) i];
        if (ac0 < 1e-9f) return;

        const int minLag = juce::jmax (2, (int) (frameRate * 60.0f / maxBpm));
        int maxLag = (int) (frameRate * 60.0f / minBpm) + 1;
        maxLag = juce::jmin (maxLag, N / 4 - 2);
        if (maxLag <= minLag + 2) return;
        const int maxAc = juce::jmin (4 * maxLag, N / 2 - 2);

        for (int lag = 1; lag <= maxAc; ++lag)
        {
            float s = 0.0f;
            for (int i = 0; i + lag < N; ++i) s += lin[(size_t) i] * lin[(size_t) (i + lag)];
            ac[(size_t) lag] = (s / ac0) * ((float) N / (float) (N - lag));
        }

// (comment)
        int best = -1; float bestScore = 0.0f, sum = 0.0f; int cnt = 0;
        for (int l = minLag; l <= maxLag; ++l)
        {
            const float b = 60.0f * frameRate / (float) l;
            const float oct = std::log2 (b / 120.0f);
            const float wgt = std::exp (-0.5f * oct * oct);
            float sc = ac[(size_t) l];
            if (2 * l <= maxAc) sc += 0.5f * ac[(size_t) (2 * l)];
            sc *= wgt;
            sum += juce::jmax (0.0f, sc); ++cnt;
            if (sc > bestScore) { bestScore = sc; best = l; }
        }
        if (best < 0) return;

        const float mean = juce::jmax (1e-6f, sum / (float) cnt);
        const float conf = bestScore / mean;
        confOut = conf;
        if (conf < 1.6f) return; // (comment)

// (comment)
        float L = parabolic (ac, best);
        for (int k : { 4, 2 })
        {
            const int c = (int) std::lround (L * (float) k);
            if (c + 2 <= maxAc)
            {
                int pk = c;
                for (int j = c - 2; j <= c + 2; ++j)
                    if (ac[(size_t) j] > ac[(size_t) pk]) pk = j;
                if (pk > 1 && pk < maxAc && ac[(size_t) pk] > 0.0f)
                {
                    L = parabolic (ac, pk) / (float) k;
                    break;
                }
            }
        }

        updateTempo (60.0f * frameRate / L);
    }

    void updateTempo (float raw)
    {
        if (bpm <= 0.0f)
        {
            if (near (raw, cand, 0.04f)) { if (++candCount >= 2) bpm = raw; }
            else { cand = raw; candCount = 1; }
        }
        else if (near (raw, bpm, 0.04f))
        {
            bpm += 0.25f * (raw - bpm); // (comment)
            candCount = 0;
        }
        else if (near (raw, bpm * 2.0f, 0.04f) || near (raw, bpm * 0.5f, 0.04f))
        {
// (comment)
        }
        else
        {
// (comment)
            if (near (raw, cand, 0.04f)) { if (++candCount >= 3) { bpm = raw; candCount = 0; } }
            else { cand = raw; candCount = 1; }
        }
        bpmOut = bpm;
    }

    double fs = 44100.0;
    int hop = 512, fftSize = 1024, envLen = 750;
    float frameRate = 86.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frame, fftData, prevMag, env, lin, ac, pref;
    int hopFill = 0, envWrite = 0, envCount = 0, sinceAnalysis = 0;
    float energyAcc = 0.0f, level = 0.0f;
    float minBpm = 70.0f, maxBpm = 180.0f;
    float bpm = 0.0f, cand = 0.0f; int candCount = 0;
    std::atomic<float> bpmOut { 0.0f }, confOut { 0.0f };
};
