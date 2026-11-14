/*
 * MAZZFLATOR - interface (skin 2026)
 * Copyright (C) 2026 Mazztock - GPL v3 (voir LICENSE)
 *
 * Tout est dessiné dans un repère fixe de 1280 x 628 (celui du skin MPC sans la barre du haut),
 * puis agrandi ou réduit selon la taille de la fenêtre. Les images sont en version x2 (Retina).
 */
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class MazzflatorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit MazzflatorEditor(MazzflatorProcessor&);
    ~MazzflatorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int W = 1280, H = 628;

private:
    struct Fader {
        juce::String id;
        float cx = 0;
        float value = 0;
        float defaultValue = 0;
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> att;
    };
    struct Toggle {
        juce::String id, img;
        juce::Rectangle<float> r;
        bool litWhenOff = false;             // FX ON : allumé quand bypass est éteint
        bool state = false;
        juce::Image on, off;
        std::unique_ptr<juce::ParameterAttachment> att;
    };

    void timerCallback() override;
    float scale() const { return (float)getWidth() / (float)W; }
    juce::Point<float> toLogical(juce::Point<float> p) const { return p / scale(); }
    void repaintLogical(juce::Rectangle<float> r);
    static float faderY(float v);            // position verticale du centre de la poignée
    juce::Rectangle<float> capRect(const Fader& f) const;
    juce::Rectangle<float> wellRect(const Fader& f) const;
    void setFaderFromDrag(Fader& f, float logicalY, bool fine);
    void showSizeMenu();
    void drawHalo(juce::Graphics& g, int frame, int doseLit);
    void drawReadout(juce::Graphics& g);

    MazzflatorProcessor& proc;
    juce::Image bg, cap, metersIn, metersOut, clipOn;
    juce::Image halo[4][8], dose[8];
    juce::Typeface::Ptr monoFace;

    Fader faders[4];
    Toggle toggles[6];
    Fader* dragging = nullptr;
    float dragStartY = 0, dragStartValue = 0;
    bool dragFine = false;

    int meterShown[4] = {-1, -1, -1, -1};
    int doseTarget = 0;
    float haloShown = 0.0f;                  // niveau affiché du halo (0..7), adouci
    int doseShown = -1;
    bool clipShown = false;
    juce::String readoutShown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MazzflatorEditor)
};
