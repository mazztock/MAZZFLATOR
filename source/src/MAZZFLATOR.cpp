/*
 * MAZZFLATOR - plugin MPC (SDK officiel Akai / inMusic)
 * Copyright (C) 2026 Mazztock
 * Dérivé de JS Inflator (Kiriki-liszt, https://github.com/Kiriki-liszt/JS_Inflator), GPL v3.
 * Projet indépendant, sans lien avec Sonnox ; « Inflator » est une marque de Sonnox Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
 *
 * Le framework du SDK (framework/mpc_plugin.h) est sous licence MIT, compatible avec la GPL v3.
 *
 * ---------------------------------------------------------------------------------------------
 * Ce fichier ne contient PAS le son : il relie le moteur V8 (MazzflatorEngine, identique à la
 * version ordinateur) à la MPC.
 *
 * Chemin du signal (dans MazzflatorEngine) :
 *   entrée -> INPUT -> [vu-mètres IN, diode CLIP] -> CLIP 0 dB -> (AA x2) -> courbe Inflator
 *          (normal / SPLIT 3 bandes / BAND médiums seuls) dosée par EFFECT -> OUTPUT -> [vu-mètres OUT]
 *   BYPASS : son intact, les vu-mètres continuent de bouger.
 *   AUTOGAIN : le moteur règle lui-même OUTPUT pour retrouver le niveau d'entrée.
 * Ici, en plus :
 *   - sortie limitée à ±1 (0 dBFS), comme la V8 qui sortait en 16 bits ;
 *   - vu-mètres, diode, dosage, OUTPUT en AUTOGAIN et texte GAIN renvoyés à la MPC.
 *
 * Paramètres : MÊMES identifiants et MÊME ordre que la V8 (params.json / params.lock), car le skin
 * désigne les paramètres par numéro (« Parameter N »). Ne jamais réordonner ; ajouter à la fin.
 * ---------------------------------------------------------------------------------------------
 */
#include "mpc_plugin.h"
#include "MazzflatorEngine.h"

#include <cmath>
#include <cstdio>

namespace {

using mazz::MazzflatorEngine;

enum ParamIndex
{
    kInput = 0,   // 0  gain d'entrée, -6 .. +18 dB (0 dB à 40 %)
    kCurve,       // 1  forme de la courbe, -50 .. +50
    kEffect,      // 2  dosage de l'effet, 0 .. 100 %
    kOutput,      // 3  gain de sortie, -24 .. 0 dB (piloté par le moteur en AUTOGAIN)
    kClip,        // 4  CLIP 0 dB (allumé par défaut)
    kSplit,       // 5  SPLIT : effet sur 3 bandes séparées
    kBypass,      // 6  BYPASS (le skin l'affiche « FX ON »)
    kInL,         // 7  vu-mètre entrée gauche   (affichage, 0..28 segments -> 0..1)
    kInR,         // 8  vu-mètre entrée droite   (affichage)
    kOutL,        // 9  vu-mètre sortie gauche   (affichage)
    kOutR,        // 10 vu-mètre sortie droite   (affichage)
    kFxDose,      // 11 dosage / halo            (affichage, 0..35 crans -> 0..1)
    kBand,        // 12 BAND : seuls les médiums 240 Hz - 2,4 kHz sont traités
    kAA,          // 13 AA : anti-aliasing x2 (latence 23 échantillons)
    kAG,          // 14 AUTOGAIN
    kAgDb,        // 15 texte « GAIN » : valeur d'OUTPUT en dB (affichage)
    kInClip,      // 16 diode CLIP d'entrée, tenue 2 s (affichage, 0 ou 1)
    kNumParams
};

// Les 10 réglages enregistrés et appliqués au moteur (les autres sont des affichages).
constexpr int kControls[] = {kInput, kCurve, kEffect, kOutput, kClip, kSplit, kBypass, kBand, kAA, kAG};

// Affichages pilotés par le moteur : index du paramètre, index dans MazzflatorEngine::meterStep, nombre de crans.
struct MeterMap { int param; int step; int steps; };
constexpr MeterMap kMeters[] = {
    {kInL, 0, MazzflatorEngine::METER_SEGMENTS}, {kInR, 1, MazzflatorEngine::METER_SEGMENTS},
    {kOutL, 2, MazzflatorEngine::METER_SEGMENTS}, {kOutR, 3, MazzflatorEngine::METER_SEGMENTS},
    {kFxDose, 4, MazzflatorEngine::DOSE_STEPS},   {kInClip, 5, 1},
};

// Comme la V8 (ancien wrapper) : la MPC est prévenue des changements au plus toutes les 1024 images,
// et le texte GAIN redessiné au plus toutes les 50 ms (2205 images).
constexpr int kNotifyFrames = 1024;
constexpr int kDisplayFrames = 2205;

} // namespace

class MAZZFLATOR : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "MAZZFLATOR";
        i.vendor = "Mazztock";
        i.uniqueId = MPC_FOURCC('M', 'z', 'F', 'l');   // == "uid" de plugin.json, identique à la V8 (4d7a466c)
        i.version = 2000;                              // 2.0.0.0
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.latencySamples = 0;                          // AA éteint par défaut
        i.tailSamples = 1;                             // pas de traîne
        return i;
    }

    MAZZFLATOR() : mpc::Plugin(makeInfo())
    {
        using mpc::floatParam;
        using mpc::boolParam;
        // Tous les réglages restent « normalisés » 0..1, exactement comme sur la V8 ;
        // le texte affiché (dB, %, -50..+50) est fabriqué par formatValue().
        addParam(floatParam("input",     "Input",    0.0f, 1.0f, (float)MazzflatorEngine::IN_ZERO_POS, "dB"));
        addParam(floatParam("curve",     "Curve",    0.0f, 1.0f, 0.5f, ""));
        addParam(floatParam("effect",    "Effect",   0.0f, 1.0f, 0.0f, "%"));
        addParam(floatParam("output",    "Output",   0.0f, 1.0f, 1.0f, "dB"));
        addParam(boolParam ("clip",      "CLIP",     true));
        addParam(boolParam ("bandsplit", "SPLIT",    false));
        addParam(boolParam ("bypass",    "BYPASS",   false));
        addParam(floatParam("in_l",      "In L",     0.0f, 1.0f, 0.0f, ""));
        addParam(floatParam("in_r",      "In R",     0.0f, 1.0f, 0.0f, ""));
        addParam(floatParam("out_l",     "Out L",    0.0f, 1.0f, 0.0f, ""));
        addParam(floatParam("out_r",     "Out R",    0.0f, 1.0f, 0.0f, ""));
        addParam(floatParam("fx_dose",   "FX Dose",  0.0f, 1.0f, 0.0f, ""));
        addParam(boolParam ("band",      "BAND",     false));
        addParam(boolParam ("aa",        "AA",       false));
        addParam(boolParam ("ag",        "AUTOGAIN", false));
        addParam(floatParam("ag_db",     "GAIN",     0.0f, 1.0f, 1.0f, "dB"));
        addParam(floatParam("in_clip",   "In Clip",  0.0f, 1.0f, 0.0f, ""));
        if (numParams() != kNumParams) mpc::log("MAZZFLATOR: nombre de paramètres incorrect");
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        engine_.setSmoothingTime(0.0);   // aucun lissage : comportement de la V8
        engine_.prepare(sampleRate);     // aucune allocation dans le moteur
    }

    // Appelé à la reprise (resume) et après le chargement d'un état ou d'un preset.
    void reset() override
    {
        engine_.reset();
        applyControls(true);
        for (int i = 0; i < kNumParams; ++i) dirty_[i] = false;
        notifyWait_ = 0;
        displayWait_ = 0;
        publishDisplays(0);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        applyControls(false);

        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const float* inL = (ctx.numInputs > 0 && ctx.inputs) ? ctx.inputs[0] : nullptr;
        const float* inR = (ctx.numInputs > 1 && ctx.inputs) ? ctx.inputs[1] : inL;

        // Le moteur travaille sur place : on recopie l'entrée si la MPC donne des tampons séparés.
        for (int i = 0; i < n; ++i)
        {
            const float l = inL ? inL[i] : 0.0f;
            const float r = inR ? inR[i] : l;
            outL[i] = l;
            if (outR) outR[i] = r;
        }

        engine_.process(outL, outR, n);

        // La V8 sortait en 16 bits : jamais au-dessus de 0 dBFS. On garde ce plafond (BYPASS compris).
        for (int i = 0; i < n; ++i)
        {
            outL[i] = mpc::clamp(outL[i], -1.0f, 1.0f);
            if (outR) outR[i] = mpc::clamp(outR[i], -1.0f, 1.0f);
        }

        publishDisplays(n);
    }

    // Texte affiché sur l'écran de la MPC (l'unité n'est pas ajoutée par la MPC : on l'écrit ici).
    bool formatValue(int index, float v, std::string& out) override
    {
        char buf[32];
        switch (index)
        {
            case kInput:  std::snprintf(buf, sizeof buf, "%+.1f dB", MazzflatorEngine::inputNormToDb(v)); break;
            case kCurve:  std::snprintf(buf, sizeof buf, "%+.0f", MazzflatorEngine::curveNormToValue(v)); break;
            case kEffect: std::snprintf(buf, sizeof buf, "%.0f %%", 100.0 * v); break;
            case kOutput:
            case kAgDb:
            {
                double db = MazzflatorEngine::outputNormToDb(v);
                if (db > -0.05) db = 0.0;               // « 0.0 dB » en haut du fader, comme la V8
                std::snprintf(buf, sizeof buf, "%.1f dB", db);
                break;
            }
            // Vu-mètres, dosage et diode : affichés en images par le skin ; leur valeur bouge avec le son et n'est
            // pas un réglage. Texte fixe, pour que la MPC n'ait rien à redessiner ni à « restaurer ».
            case kInL: case kInR: case kOutL: case kOutR: case kFxDose: case kInClip:
                out = "--";
                return true;
            default: return false;
        }
        if (std::strcmp(buf, "+0.0 dB") == 0 || std::strcmp(buf, "-0.0 dB") == 0) std::snprintf(buf, sizeof buf, "0.0 dB");
        if (std::strcmp(buf, "+0") == 0 || std::strcmp(buf, "-0") == 0) std::snprintf(buf, sizeof buf, "0");
        out = buf;
        return true;
    }

private:
    // Applique au moteur les réglages qui ont changé depuis le dernier bloc. Lus une fois par bloc, sur le fil
    // audio : le « secouage » du paramètre 0 par la MPC (aller-retour avant le premier bloc) reste sans effet.
    void applyControls(bool all)
    {
        for (int idx : kControls)
        {
            const float v = getNormalised(idx);
            if (!all && v == applied_[idx]) continue;
            applied_[idx] = v;
            const double d = (double)v;
            const bool on = v >= 0.5f;
            switch (idx)
            {
                case kInput:  engine_.setInput(d); break;
                case kCurve:  engine_.setCurve(d); break;
                case kEffect: engine_.setEffect(d); break;
                case kOutput:
                    // all = relecture d'un état : la valeur enregistrée s'impose même en AUTOGAIN.
                    // Sinon, en AUTOGAIN, le moteur ignore le fader ; publishDisplays() le remet à sa place.
                    engine_.setOutput(d, all);
                    break;
                case kClip:   engine_.setClip(on); break;
                case kSplit:  engine_.setSplit(on); break;
                case kBypass: engine_.setBypass(on); break;
                case kBand:   engine_.setBand(on); break;
                case kAA:
                    engine_.setAA(on);
                    // Latence annoncée sans rappel à l'hôte (pas d'appel IOChanged depuis le fil audio).
                    if (effect_) effect_->initialDelay = engine_.getLatencySamples();
                    info_.latencySamples = engine_.getLatencySamples();
                    break;
                case kAG:     engine_.setAutoGain(on); break;
                default: break;
            }
        }
    }

    // Écrit un paramètre d'affichage s'il a changé, et le marque « à signaler » à la MPC.
    void publish(int idx, float v)
    {
        if (getNormalised(idx) == v) return;
        setNormalised(idx, v);
        dirty_[idx] = true;
    }

    // Recopie vu-mètres, diode, dosage, OUTPUT (AUTOGAIN) et GAIN dans leurs paramètres, puis prévient la MPC.
    void publishDisplays(int frames)
    {
        for (const MeterMap& m : kMeters)
            publish(m.param, (float)engine_.meterStep(m.step) / (float)m.steps);

        // OUTPUT appartient au moteur en AUTOGAIN : le fader suit le moteur, et reprend sa place si on le bouge.
        const float outNorm = (float)engine_.getOutput();
        const bool userMoved = userMovedOutput_.exchange(false, std::memory_order_relaxed);
        if (engine_.getAutoGain() && getNormalised(kOutput) != outNorm)
        {
            selfWrite_ = true;                           // ce n'est pas un geste de l'utilisateur
            setNormalised(kOutput, outNorm);
            selfWrite_ = false;
            applied_[kOutput] = outNorm;
            if (std::fabs(outNorm - outReported_) >= 1.0f / 70.0f || userMoved) dirty_[kOutput] = true;
        }
        if (dirty_[kOutput]) outReported_ = outNorm;

        // Texte GAIN : redessiné quand il change (au plus toutes les 50 ms).
        publish(kAgDb, outNorm);
        const int gainTenths = (int)std::lround(10.0 * MazzflatorEngine::outputNormToDb(outNorm));
        if (gainTenths != shownGainTenths_) { shownGainTenths_ = gainTenths; displayPending_ = true; }

        if (frames <= 0) return;                         // reset() : rien n'est envoyé hors du traitement audio
        notifyWait_ -= frames;
        if (notifyWait_ <= 0)
        {
            notifyWait_ = kNotifyFrames;
            for (int i = 0; i < kNumParams; ++i)
                if (dirty_[i]) { dirty_[i] = false; notifyHostParameterChanged(i); }
        }
        displayWait_ -= frames;
        if (displayPending_ && displayWait_ <= 0)
        {
            displayPending_ = false;
            displayWait_ = kDisplayFrames;
            requestDisplayUpdate();
        }
    }

public:
    // Le moteur peut ignorer OUTPUT (AUTOGAIN) : on note le geste pour remettre le fader à sa place.
    void parameterChanged(int index, float /*plain*/) override
    {
        if (index == kOutput && !selfWrite_) userMovedOutput_.store(true, std::memory_order_relaxed);
    }

private:
    MazzflatorEngine engine_;
    float applied_[kNumParams] = {};
    bool dirty_[kNumParams] = {};
    int notifyWait_ = 0;
    int displayWait_ = 0;
    bool displayPending_ = false;
    int shownGainTenths_ = 0;
    float outReported_ = 1.0f;
    std::atomic<bool> selfWrite_{false};                      // vrai pendant que le plugin écrit OUTPUT lui-même (fil audio)
    std::atomic<bool> userMovedOutput_{false};
};

MPC_PLUGIN_ENTRY(MAZZFLATOR)
