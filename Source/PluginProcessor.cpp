#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // length of one delay repeat in beats
    const float kDivBeats[] = { 2.0f, 1.0f, 1.5f, 0.6667f, 0.5f, 0.75f, 0.3333f, 0.25f, 0.375f };

    inline float softClip (float x)
    {
        const float a = std::abs (x);
        if (a < 0.9f) return x;
        const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f);
        return x < 0.0f ? -y : y;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SyncDelayProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto bpm = [] (float v, int) { return String (v, 1) + " BPM"; };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                   StringArray { "Analyzer", "Delay" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "division", 1 }, "Division",
                                                   StringArray { "1/2", "1/4", "1/4 dotted", "1/4 triplet", "1/8",
                                                                 "1/8 dotted", "1/8 triplet", "1/16", "1/16 dotted" }, 1));

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "feedback", 1 }, "Feedback",
                                                  NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.35f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lowcut", 1 }, "Low Cut",
                                                  NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), 150.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "highcut", 1 }, "High Cut",
                                                  NormalisableRange<float> (1000.0f, 20000.0f, 1.0f, 0.4f), 7000.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "pingpong", 1 }, "Ping-Pong",
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mod", 1 }, "Mod",
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dry", 1 }, "Dry",
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "wet", 1 }, "Wet",
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide",
                                                  NormalisableRange<float> (20.0f, 3000.0f, 1.0f, 0.5f), 400.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (ms)));

    l.add (std::make_unique<AudioParameterBool> (ParameterID { "follow", 1 }, "Follow Music", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "lock", 1 }, "Lock Tempo", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "manual", 1 }, "Manual BPM",
                                                  NormalisableRange<float> (40.0f, 220.0f, 0.1f), 120.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (bpm)));

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "minbpm", 1 }, "Min BPM",
                                                  NormalisableRange<float> (50.0f, 120.0f, 1.0f), 70.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (bpm)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "maxbpm", 1 }, "Max BPM",
                                                  NormalisableRange<float> (100.0f, 220.0f, 1.0f), 180.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (bpm)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "snap", 1 }, "Snap to whole BPM", true));
    return l;
}

SyncDelayProcessor::SyncDelayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool SyncDelayProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SyncDelayProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    len = (int) (fs * 4.0) + 8;
    for (auto& b : buf) b.assign ((size_t) len, 0.0f);
    writePos = 0;
    modPhase = 0.0;
    for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); }
    curDelay = targetDelay = 0.5 * fs;
    snappedBpm = heldBpm = 0.0f;
    tracker.prepare (fs);

    dryS.reset (fs, 0.03);
    wetS.reset (fs, 0.03);
    fbS.reset (fs, 0.03);
    dryS.setCurrentAndTargetValue (apvts.getRawParameterValue ("dry")->load());
    wetS.setCurrentAndTargetValue (apvts.getRawParameterValue ("wet")->load());
    fbS.setCurrentAndTargetValue (apvts.getRawParameterValue ("feedback")->load());
}

void SyncDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || len == 0 || n == 0) return;

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // ---------------------------------------------------------------- ANALYZER mode
    // Audio is NOT touched. We only listen to the music and publish the tempo.
    if (get ("mode") < 0.5f)
    {
        tracker.setRange (get ("minbpm"), get ("maxbpm"));
        const float* aL = buffer.getReadPointer (0);
        const float* aR = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i)
            tracker.pushSample (0.5f * (aL[i] + aR[i]));

        const float b = tracker.getBpm();
        if (b > 0.0f)
        {
            float out = b;
            if (get ("snap") > 0.5f)
            {
                // round to whole BPM, with hysteresis so the value stays rock steady
                if (snappedBpm <= 0.0f || std::abs (b - snappedBpm) > 0.6f)
                    snappedBpm = std::round (b);
                out = snappedBpm;
            }
            else
            {
                snappedBpm = 0.0f;
            }
            sharedTempo().bpm.store (out);
            sharedTempo().stamp.store (juce::Time::getMillisecondCounterHiRes());
            shownBpm.store (out);
        }
        return;
    }

    // ---------------------------------------------------------------- DELAY mode
    const bool follow = get ("follow") > 0.5f;
    const bool lock = get ("lock") > 0.5f;
    const float pp = get ("pingpong");
    const float modDepth = get ("mod");
    const float beats = kDivBeats[juce::jlimit (0, 8, (int) std::lround (get ("division")))];

    const float shared = sharedTempo().bpm.load();
    float bpm = (follow && shared > 0.0f) ? shared : get ("manual");
    if (lock)
    {
        if (heldBpm <= 0.0f) heldBpm = bpm;
        bpm = heldBpm;
    }
    else
    {
        heldBpm = 0.0f;
    }
    shownBpm.store (bpm);

    targetDelay = juce::jlimit (0.01 * fs, (double) (len - 16), (60.0 / (double) bpm) * (double) beats * fs);

    const double glideMs = juce::jmax (1.0f, get ("glide"));
    const double glideCoef = 1.0 - std::exp (-1.0 / (glideMs * 0.001 * fs));
    const float modSamples = modDepth * 0.004f * (float) fs;       // up to +-4 ms wobble
    const double modInc = 0.6 / fs;                                // 0.6 Hz

    for (int c = 0; c < 2; ++c)
    {
        hp[c].set (true, fs, get ("lowcut"));
        lp[c].set (false, fs, get ("highcut"));
    }

    dryS.setTargetValue (get ("dry"));
    wetS.setTargetValue (get ("wet"));
    fbS.setTargetValue (get ("feedback"));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    float* dl = buf[0].data();
    float* dr = buf[1].data();

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];

        curDelay += (targetDelay - curDelay) * glideCoef;
        modPhase += modInc;
        if (modPhase >= 1.0) modPhase -= 1.0;
        const float lfo = std::sin (2.0f * juce::MathConstants<float>::pi * (float) modPhase) * modSamples;

        const double dLeft  = juce::jlimit (1.0, (double) (len - 4), curDelay + (double) lfo);
        const double dRight = juce::jlimit (1.0, (double) (len - 4), curDelay - (double) lfo);

        double rpL = (double) writePos - dLeft;
        double rpR = (double) writePos - dRight;
        while (rpL < 0.0) rpL += (double) len;
        while (rpR < 0.0) rpR += (double) len;

        const int l0 = (int) rpL, l1 = (l0 + 1) % len;
        const int r0 = (int) rpR, r1 = (r0 + 1) % len;
        const float lf = (float) (rpL - (double) l0);
        const float rf = (float) (rpR - (double) r0);

        const float wetL = dl[l0] * (1.0f - lf) + dl[l1] * lf;
        const float wetR = dr[r0] * (1.0f - rf) + dr[r1] * rf;

        const float fb = fbS.getNextValue();
        const float dry = dryS.getNextValue();
        const float wet = wetS.getNextValue();

        // ping-pong: 0 = two independent channels, 1 = classic left/right bounce
        const float mono = 0.5f * (inL + inR);
        const float toL = inL * (1.0f - pp) + mono * pp;
        const float toR = inR * (1.0f - pp);
        const float fbL = wetL * (1.0f - pp) + wetR * pp;
        const float fbR = wetR * (1.0f - pp) + wetL * pp;

        float xl = toL + fb * fbL;
        float xr = toR + fb * fbR;

        // low cut + high cut on everything that enters the delay line (repeats get thinner each pass)
        xl = lp[0].process (hp[0].process (xl));
        xr = lp[1].process (hp[1].process (xr));

        dl[writePos] = softClip (xl);
        dr[writePos] = softClip (xr);

        L[i] = inL * dry + wetL * wet;
        R[i] = inR * dry + wetR * wet;

        if (++writePos >= len) writePos = 0;
    }

    shownDelayMs.store ((float) (curDelay / fs * 1000.0));
}

juce::AudioProcessorEditor* SyncDelayProcessor::createEditor()
{
    return new SyncDelayEditor (*this);
}

void SyncDelayProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SyncDelayProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SyncDelayProcessor();
}
