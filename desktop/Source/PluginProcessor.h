/*
 * MAZZFLATOR - plugin ordinateur (VST3 / AU / CLAP)
 * Copyright (C) 2026 Mazztock - GPL v3 (voir LICENSE)
 */
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "MazzflatorEngine.h"

class MazzflatorProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    MazzflatorProcessor();
    ~MazzflatorProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    // vu-mètres pour l'interface (écrits par le son, lus par l'écran)
    std::atomic<int> meterStep[6] = {};
    std::atomic<float> agOutput { 1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    template <typename T> void processAny(juce::AudioBuffer<T>& buffer);
    void syncParameters();
    void timerCallback() override;

    mazz::MazzflatorEngine engine;
    juce::AudioProcessorParameter* bypassParam = nullptr;
    std::atomic<float>* pInput = nullptr; std::atomic<float>* pCurve = nullptr;
    std::atomic<float>* pEffect = nullptr; std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pClip = nullptr; std::atomic<float>* pSplit = nullptr;
    std::atomic<float>* pBand = nullptr; std::atomic<float>* pAA = nullptr;
    std::atomic<float>* pAG = nullptr; std::atomic<float>* pBypass = nullptr;
    std::atomic<bool> forceOutput { true };     // relecture d'un état : OUTPUT s'impose même en AUTOGAIN
    int reportedLatency = -1;
    static constexpr int kDelaySize = 64;          // > latence AA (23)
    double bypassDelay[2][kDelaySize] = {};
    int bypassPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MazzflatorProcessor)
};
