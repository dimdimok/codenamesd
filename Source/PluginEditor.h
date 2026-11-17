#pragma once
#include "PluginProcessor.h"

namespace sd
{
    const juce::Colour kBg     (0xff0f1115);
    const juce::Colour kPanel  (0xff181b21);
    const juce::Colour kAccent (0xffff8a3d);
    const juce::Colour kTrack  (0xff2b2f38);
    const juce::Colour kField  (0xff23272f);
    const juce::Colour kText   (0xffe8eaed);
    const juce::Colour kDim    (0xff8b919c);
}

class SyncLNF : public juce::LookAndFeel_V4
{
public:
    SyncLNF()
    {
        setColour (juce::Slider::textBoxTextColourId, sd::kText);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, sd::kText);
        setColour (juce::ComboBox::backgroundColourId, sd::kField);
        setColour (juce::ComboBox::textColourId, sd::kText);
        setColour (juce::ComboBox::outlineColourId, sd::kTrack);
        setColour (juce::ComboBox::arrowColourId, sd::kAccent);
        setColour (juce::PopupMenu::backgroundColourId, sd::kField);
        setColour (juce::PopupMenu::textColourId, sd::kText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, sd::kAccent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::TextButton::buttonColourId, sd::kField);
        setColour (juce::TextButton::buttonOnColourId, sd::kAccent);
        setColour (juce::TextButton::textColourOffId, sd::kText);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const float r = d * 0.5f;
        const auto c = b.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour (sd::kTrack);
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, angle, true);
        g.setColour (sd::kAccent);
        g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float br = r * 0.70f;
        g.setColour (juce::Colour (0xff22262e));
        g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
        g.setColour (juce::Colour (0xff343944));
        g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.5f);

        juce::Path p;
        p.addRoundedRectangle (-1.8f, -br + 3.0f, 3.6f, br * 0.45f, 1.8f);
        g.setColour (sd::kText);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }
};

class SyncDelayEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SyncDelayEditor (SyncDelayProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p)
    {
        setLookAndFeel (&lnf);

        // mode switch
        modeBox.addItem ("ANALYZER  (music track)", 1);
        modeBox.addItem ("DELAY  (vocal track)", 2);
        addAndMakeVisible (modeBox);
        modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);

        // big tempo readout
        bpmLabel.setFont (juce::Font (juce::FontOptions (46.0f)));
        bpmLabel.setColour (juce::Label::textColourId, sd::kAccent);
        bpmLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (bpmLabel);

        sourceLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
        sourceLabel.setColour (juce::Label::textColourId, sd::kDim);
        sourceLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (sourceLabel);

        // delay-mode controls
        if (auto* dp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("division")))
            divBox.addItemList (dp->choices, 1);
        addChildComponent (divBox);
        divAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "division", divBox);
        delayGroup.push_back (&divBox);

        divLabel.setText ("DIVISION", juce::dontSendNotification);
        divLabel.setColour (juce::Label::textColourId, sd::kDim);
        divLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addChildComponent (divLabel);
        delayGroup.push_back (&divLabel);

        setupToggle (followBtn, "FOLLOW MUSIC", "follow", followAtt);
        setupToggle (lockBtn, "LOCK TEMPO", "lock", lockAtt);
        delayGroup.push_back (&followBtn);
        delayGroup.push_back (&lockBtn);

        addKnob (feedbackK, "FEEDBACK", "feedback", delayGroup);
        addKnob (lowcutK, "LOW CUT", "lowcut", delayGroup);
        addKnob (highcutK, "HIGH CUT", "highcut", delayGroup);
        addKnob (ppK, "PING-PONG", "pingpong", delayGroup);
        addKnob (modK, "MOD", "mod", delayGroup);
        addKnob (dryK, "DRY", "dry", delayGroup);
        addKnob (wetK, "WET", "wet", delayGroup);
        addKnob (glideK, "GLIDE", "glide", delayGroup);
        addKnob (manualK, "MANUAL BPM", "manual", delayGroup);

        // analyzer-mode controls
        addKnob (minK, "MIN BPM", "minbpm", analyzerGroup);
        addKnob (maxK, "MAX BPM", "maxbpm", analyzerGroup);
        setupToggle (snapBtn, "SNAP TO WHOLE BPM", "snap", snapAtt);
        analyzerGroup.push_back (&snapBtn);

        helpLabel.setText ("Put this plugin on the MUSIC track and leave it in ANALYZER mode. "
                           "It only listens: the music passes through unchanged and nothing is added to it.\n\n"
                           "Then put another Sync Delay on each VOCAL track and switch it to DELAY mode. "
                           "They will automatically follow the tempo found here.",
                           juce::dontSendNotification);
        helpLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
        helpLabel.setColour (juce::Label::textColourId, sd::kDim);
        helpLabel.setJustificationType (juce::Justification::topLeft);
        addChildComponent (helpLabel);
        analyzerGroup.push_back (&helpLabel);

        setSize (800, 470);
        updateMode (true);
        timerCallback();
        startTimerHz (10);
    }

    ~SyncDelayEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (sd::kBg);

        g.setColour (sd::kPanel);
        g.fillRoundedRectangle (panelArea.expanded (8.0f, 8.0f), 12.0f);

        g.setColour (sd::kAccent);
        g.setFont (24.0f);
        g.drawText ("SYNC DELAY", 18, 14, 260, 30, juce::Justification::centredLeft);
        g.setColour (sd::kDim);
        g.setFont (12.0f);
        g.drawText ("tempo follows the music", 20, 40, 260, 16, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        auto top = r.removeFromTop (46);
        modeBox.setBounds (top.removeFromRight (250).withTrimmedTop (6).withTrimmedBottom (6));
        r.removeFromTop (8);

        auto info = r.removeFromTop (92);
        auto infoLeft = info.removeFromLeft (300);
        bpmLabel.setBounds (infoLeft.removeFromTop (56));
        sourceLabel.setBounds (infoLeft);

        auto ctl = info.reduced (0, 6);
        auto divArea = ctl.removeFromLeft (170);
        divLabel.setBounds (divArea.removeFromTop (18));
        divBox.setBounds (divArea.removeFromTop (34));
        ctl.removeFromLeft (10);
        auto f = ctl.removeFromLeft (136);
        followBtn.setBounds (f.withTrimmedTop (18).withHeight (34));
        ctl.removeFromLeft (10);
        auto l = ctl.removeFromLeft (130);
        lockBtn.setBounds (l.withTrimmedTop (18).withHeight (34));

        r.removeFromTop (10);
        panelArea = r.toFloat();

        const int cellW = r.getWidth() / 5;
        const int cellH = r.getHeight() / 2;
        auto cell = [&] (int col, int row) { return juce::Rectangle<int> (r.getX() + col * cellW, r.getY() + row * cellH, cellW, cellH); };

        place (feedbackK, cell (0, 0));
        place (lowcutK,   cell (1, 0));
        place (highcutK,  cell (2, 0));
        place (ppK,       cell (3, 0));
        place (modK,      cell (4, 0));
        place (dryK,      cell (0, 1));
        place (wetK,      cell (1, 1));
        place (glideK,    cell (2, 1));
        place (manualK,   cell (3, 1));

        place (minK, cell (0, 0));
        place (maxK, cell (1, 0));
        snapBtn.setBounds (cell (2, 0).withSizeKeepingCentre (cellW - 10, 36));
        helpLabel.setBounds (juce::Rectangle<int> (r.getX() + 16, r.getY() + cellH + 4, r.getWidth() - 32, cellH - 8));
    }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void addKnob (Knob& k, const juce::String& text, const char* id, std::vector<juce::Component*>& group)
    {
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 90, 18);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
        k.label.setText (text, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, sd::kDim);
        k.label.setFont (juce::Font (juce::FontOptions (12.0f)));
        addChildComponent (k.slider);
        addChildComponent (k.label);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
        group.push_back (&k.slider);
        group.push_back (&k.label);
    }

    void setupToggle (juce::TextButton& b, const juce::String& text, const char* id,
                      std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (true);
        addChildComponent (b);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, b);
    }

    static void place (Knob& k, juce::Rectangle<int> cell)
    {
        k.label.setBounds (cell.removeFromTop (18));
        k.slider.setBounds (cell);
    }

    void updateMode (bool force)
    {
        const int m = proc.isAnalyzerMode() ? 1 : 0;
        if (! force && m == lastMode) return;
        lastMode = m;
        for (auto* c : delayGroup)    c->setVisible (m == 0);
        for (auto* c : analyzerGroup) c->setVisible (m == 1);
    }

    void timerCallback() override
    {
        updateMode (false);
        const float bpm = proc.getShownBpm();

        if (proc.isAnalyzerMode())
        {
            bpmLabel.setText (bpm > 0.0f ? juce::String (bpm, 1) + " BPM" : juce::String ("listening..."),
                              juce::dontSendNotification);
            sourceLabel.setText ("ANALYZER - listening to the music (takes 4-8 sec to lock)", juce::dontSendNotification);
        }
        else
        {
            bpmLabel.setText (juce::String (bpm, 1) + " BPM", juce::dontSendNotification);

            const bool follow = proc.apvts.getRawParameterValue ("follow")->load() > 0.5f;
            const bool lock = proc.apvts.getRawParameterValue ("lock")->load() > 0.5f;

            juce::String src;
            if (lock)             src = "LOCKED - tempo is frozen";
            else if (! follow)    src = "manual tempo";
            else if (proc.isAnalyzerAlive()) src = "following the music analyzer";
            else                  src = "no analyzer found - using Manual BPM";

            src << "\ndelay time: " << juce::String (proc.getShownDelayMs(), 0) << " ms";
            sourceLabel.setText (src, juce::dontSendNotification);
        }
    }

    SyncLNF lnf;
    SyncDelayProcessor& proc;

    juce::ComboBox modeBox, divBox;
    juce::Label bpmLabel, sourceLabel, divLabel, helpLabel;
    juce::TextButton followBtn, lockBtn, snapBtn;

    Knob feedbackK, lowcutK, highcutK, ppK, modK, dryK, wetK, glideK, manualK, minK, maxK;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, divAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAtt, lockAtt, snapAtt;

    std::vector<juce::Component*> delayGroup, analyzerGroup;
    juce::Rectangle<float> panelArea;
    int lastMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncDelayEditor)
};
