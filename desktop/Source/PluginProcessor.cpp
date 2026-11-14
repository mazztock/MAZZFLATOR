/*
 * MAZZFLATOR - plugin ordinateur (VST3 / AU / CLAP)
 * Copyright (C) 2026 Mazztock - GPL v3 (voir LICENSE)
 */
#include "PluginProcessor.h"
#include "PluginEditor.h"

using Engine = mazz::MazzflatorEngine;

// ---------------------------------------------------------------- paramètres (mêmes clés et mêmes courses que sur la MPC)
juce::AudioProcessorValueTreeState::ParameterLayout MazzflatorProcessor::createLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const NormalisableRange<float> course(0.0f, 1.0f);

    auto dbText = [](double db) {
        if (std::abs(db) < 0.05) db = 0.0;
        return (db > 0.0 ? "+" : "") + String(db, 1) + " dB";
    };

    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"input", 1}, "Input", course, (float)Engine::IN_ZERO_POS,
        AudioParameterFloatAttributes().withLabel("dB")
            .withStringFromValueFunction([dbText](float v, int) { return dbText(Engine::inputNormToDb(v)); })
            .withValueFromStringFunction([](const String& s) {
                double db = s.getDoubleValue();
                double v = (db < 0.0) ? Engine::IN_ZERO_POS * (1.0 - db / Engine::IN_MIN_DB)
                                      : Engine::IN_ZERO_POS + db / Engine::IN_MAX_DB * (1.0 - Engine::IN_ZERO_POS);
                return (float)jlimit(0.0, 1.0, v); })));

    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"effect", 1}, "Effect", course, 0.0f,
        AudioParameterFloatAttributes().withLabel("%")
            .withStringFromValueFunction([](float v, int) { return String(v * 100.0f, 0) + " %"; })
            .withValueFromStringFunction([](const String& s) { return jlimit(0.0f, 1.0f, s.getFloatValue() / 100.0f); })));

    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"curve", 1}, "Curve", course, 0.5f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) {
                double c = Engine::curveNormToValue(v); if (std::abs(c) < 0.05) c = 0.0;
                return (c > 0.0 ? "+" : "") + String(c, 1); })
            .withValueFromStringFunction([](const String& s) {
                return jlimit(0.0f, 1.0f, (float)(s.getDoubleValue() / (100.0 * Engine::CURVE_SCALE) + 0.5)); })));

    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"output", 1}, "Output", course, 1.0f,
        AudioParameterFloatAttributes().withLabel("dB")
            .withStringFromValueFunction([dbText](float v, int) { return dbText(Engine::outputNormToDb(v)); })
            .withValueFromStringFunction([](const String& s) {
                return jlimit(0.0f, 1.0f, (float)((s.getDoubleValue() + Engine::OUT_RANGE_DB) / Engine::OUT_RANGE_DB)); })));

    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"clip", 1}, "CLIP 0dB", true));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"bandsplit", 1}, "SPLIT", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"band", 1}, "BAND", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"aa", 1}, "AA", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"ag", 1}, "AUTO GAIN", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"bypass", 1}, "Bypass", false));
    return { p.begin(), p.end() };
}

MazzflatorProcessor::MazzflatorProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MAZZFLATOR", createLayout()) {
    pInput = apvts.getRawParameterValue("input");   pCurve = apvts.getRawParameterValue("curve");
    pEffect = apvts.getRawParameterValue("effect"); pOutput = apvts.getRawParameterValue("output");
    pClip = apvts.getRawParameterValue("clip");     pSplit = apvts.getRawParameterValue("bandsplit");
    pBand = apvts.getRawParameterValue("band");     pAA = apvts.getRawParameterValue("aa");
    pAG = apvts.getRawParameterValue("ag");         pBypass = apvts.getRawParameterValue("bypass");
    bypassParam = apvts.getParameter("bypass");
    engine.setSmoothingTime(0.02);                  // 20 ms : pas de clic quand on bouge un fader
    startTimerHz(30);
}

MazzflatorProcessor::~MazzflatorProcessor() { stopTimer(); }

bool MazzflatorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void MazzflatorProcessor::prepareToPlay(double sampleRate, int) {
    engine.prepare(sampleRate);
    forceOutput = true;
    syncParameters();
    for (auto& ch : bypassDelay) std::fill(std::begin(ch), std::end(ch), 0.0);
    bypassPos = 0;
    reportedLatency = engine.getLatencySamples();
    setLatencySamples(reportedLatency);
}

// lit les réglages de l'hôte au début de chaque bloc
void MazzflatorProcessor::syncParameters() {
    engine.setAutoGain(pAG->load() > 0.5f);
    engine.setInput(pInput->load());
    engine.setCurve(pCurve->load());
    engine.setEffect(pEffect->load());
    engine.setOutput(pOutput->load(), forceOutput.exchange(false));
    engine.setClip(pClip->load() > 0.5f);
    engine.setSplit(pSplit->load() > 0.5f);
    engine.setBand(pBand->load() > 0.5f);
    engine.setAA(pAA->load() > 0.5f);
    engine.setBypass(pBypass->load() > 0.5f);
}

template <typename T>
void MazzflatorProcessor::processAny(juce::AudioBuffer<T>& buffer) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin(buffer.getNumChannels(), 2);
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); c++) buffer.clear(c, 0, n);
    if (nch == 0 || n == 0) return;

    syncParameters();
    const int lat = engine.getLatencySamples();
    if (lat != reportedLatency) { reportedLatency = lat; setLatencySamples(lat); }

    T* L = buffer.getWritePointer(0);
    T* R = nch > 1 ? buffer.getWritePointer(1) : nullptr;
    engine.process(L, R, n);

    // BYPASS avec AA : le son direct est retardé de la même latence pour rester calé
    if (engine.getBypass() && lat > 0) {
        for (int i = 0; i < n; i++) {
            int rd = (bypassPos - lat + kDelaySize) % kDelaySize;
            for (int c = 0; c < nch; c++) {
                T* d = buffer.getWritePointer(c);
                bypassDelay[c][bypassPos] = (double)d[i];
                d[i] = (T)bypassDelay[c][rd];
            }
            bypassPos = (bypassPos + 1) % kDelaySize;
        }
    }

    for (int i = 0; i < 6; i++) meterStep[i].store(engine.meterStep(i), std::memory_order_relaxed);
    agOutput.store((float)engine.getOutput(), std::memory_order_relaxed);
}

void MazzflatorProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { processAny(b); }
void MazzflatorProcessor::processBlock(juce::AudioBuffer<double>& b, juce::MidiBuffer&) { processAny(b); }

// AUTOGAIN : le fader OUTPUT de l'hôte suit la valeur choisie par le moteur
void MazzflatorProcessor::timerCallback() {
    if (pAG->load() < 0.5f) return;
    auto* out = apvts.getParameter("output");
    float v = agOutput.load(std::memory_order_relaxed);
    if (std::abs(out->getValue() - v) > 0.001f) {
        out->beginChangeGesture();
        out->setValueNotifyingHost(v);
        out->endChangeGesture();
    }
}

// ---------------------------------------------------------------- sauvegarde des réglages avec le projet
void MazzflatorProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void MazzflatorProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            forceOutput = true;
        }
}

juce::AudioProcessorEditor* MazzflatorProcessor::createEditor() {
    return new MazzflatorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MazzflatorProcessor(); }
