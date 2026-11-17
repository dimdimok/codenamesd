#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BeatTracker.h"

// Shared tempo "board": the Analyzer instance (on the music track) writes the BPM here,
// every Delay instance (on the vocal tracks) reads it. Works between instances in one host process.
struct SharedTempo
{
    std::atomic<float> bpm { 0.0f };
    std::atomic<double> stamp { 0.0 };
};
inline SharedTempo& sharedTempo() { static SharedTempo s; return s; }

// 2nd order (12 dB/oct) filter
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set (bool highpass, double fs, float freq)
    {
        freq = juce::jlimit (10.0f, (float) (fs * 0.45), freq);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * freq / (float) fs;
        const float c = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * 0.7071f);
        const float a0 = 1.0f + alpha;
        if (highpass)
        {
            b0 = ((1.0f + c) * 0.5f) / a0;
            b1 = -(1.0f + c) / a0;
            b2 = b0;
        }
        else
        {
            b0 = ((1.0f - c) * 0.5f) / a0;
            b1 = (1.0f - c) / a0;
            b2 = b0;
        }
        a1 = (-2.0f * c) / a0;
        a2 = (1.0f - alpha) / a0;
    }
};

class SyncDelayProcessor : public juce::AudioProcessor
{
public:
    SyncDelayProcessor();
    ~SyncDelayProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sync Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // for the GUI
    bool isAnalyzerMode() const { return apvts.getRawParameterValue ("mode")->load() < 0.5f; }
    float getShownBpm() const { return shownBpm.load(); }
    float getShownDelayMs() const { return shownDelayMs.load(); }
    bool isAnalyzerAlive() const
    {
        const double age = (juce::Time::getMillisecondCounterHiRes() - sharedTempo().stamp.load()) / 1000.0;
        return sharedTempo().bpm.load() > 0.0f && age < 5.0;
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    BeatTracker tracker;
    double fs = 44100.0;
    std::vector<float> buf[2];
    int len = 0, writePos = 0;
    double curDelay = 22050.0, targetDelay = 22050.0, modPhase = 0.0;
    Biquad hp[2], lp[2];
    float snappedBpm = 0.0f, heldBpm = 0.0f;
    juce::SmoothedValue<float> dryS, wetS, fbS;
    std::atomic<float> shownBpm { 0.0f }, shownDelayMs { 0.0f };
};
