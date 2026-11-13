/*
 * MAZZFLATOR - moteur audio indépendant (version ordinateur)
 * Copyright (C) 2026 Mazztock
 * Dérivé de JS Inflator (Kiriki-liszt), GPL v3.
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version. See LICENSE.
 *
 * Même son que le moteur V8 de la MPC (mpc_wrapper.cpp), mais :
 *  - calcul en double, entrée/sortie en float ou double, sans conversion 16 bits ni écrêtage final ;
 *  - toutes fréquences d'échantillonnage (filtres SPLIT/BAND, AA x2, constantes de temps) ;
 *  - lissage optionnel des réglages (aucun clic quand on bouge un fader).
 * Les réglages restent « normalisés » 0..1 exactement comme sur la MPC.
 */
#pragma once

namespace mazz {

class MazzflatorEngine {
public:
    // --- constantes reprises telles quelles du moteur V8 ---
    static constexpr int    AA_M = 23;              // demi-longueur du filtre demi-bande ; latence = AA_M échantillons
    static constexpr double AA_BETA = 8.0;          // fenêtre de Kaiser
    static constexpr double IN_MIN_DB = -6.0;       // INPUT : -6 .. +18 dB, 0 dB à 40 % de la course
    static constexpr double IN_MAX_DB = 18.0;
    static constexpr double IN_ZERO_POS = 0.4;
    static constexpr double CURVE_SCALE = 1.0;      // CURVE -50 .. +50
    static constexpr double OUT_RANGE_DB = 24.0;    // OUTPUT : 0 .. -24 dB
    static constexpr double CLIP_HOLD_S = 2.0;      // tenue de la diode CLIP d'entrée
    static constexpr double DOSE_RELEASE_S = 0.07;  // retombée du dosage
    static constexpr double METER_RELEASE_S = 0.3;  // retombée des vu-mètres
    static constexpr double BAND_LOW_BOOST_DB = 3.0;
    static constexpr double BAND_HIGH_BOOST_DB = 2.0;
    static constexpr double AG_TAU = 1.5;
    static constexpr double AG_SLEW_DB_PER_S = 4.0;
    static constexpr double SPLIT_LOW_HZ = 240.0;
    static constexpr double SPLIT_HIGH_HZ = 2400.0;
    static constexpr int    METER_SEGMENTS = 28;
    static constexpr int    DOSE_STEPS = 35;

    MazzflatorEngine();

    void prepare(double sampleRate);    // à appeler avant de jouer (et à chaque changement de fréquence)
    void reset();                       // vide les filtres et les vu-mètres
    void setSmoothingTime(double seconds);   // 0 = aucun lissage (comportement MPC)

    // réglages normalisés 0..1 (mêmes valeurs que params.json)
    void setInput(double v);
    void setCurve(double v);
    void setEffect(double v);
    void setOutput(double v, bool force = false);   // ignoré en AUTOGAIN sauf si force (relecture d'un état)
    void setClip(bool on);
    void setSplit(bool on);
    void setBand(bool on);
    void setAA(bool on);
    void setAutoGain(bool on);
    void setBypass(bool on);

    double getInput()  const { return fInput; }
    double getCurve()  const { return fCurve; }
    double getEffect() const { return fEffect; }
    double getOutput() const { return fOutput; }    // en AUTOGAIN, c'est le moteur qui la fait bouger
    bool getClip() const { return bClip; }
    bool getSplit() const { return bSplit; }
    bool getBand() const { return bBand; }
    bool getAA() const { return bAA; }
    bool getAutoGain() const { return bAG; }
    bool getBypass() const { return bBypass; }
    int  getLatencySamples() const { return bAA ? AA_M : 0; }
    double getSampleRate() const { return sampleRate; }

    static double inputNormToDb(double v);
    static double outputNormToDb(double v) { return OUT_RANGE_DB * v - OUT_RANGE_DB; }
    static double curveNormToValue(double v) { return (v - 0.5) * CURVE_SCALE * 100.0; }

    // traitement sur place ; R peut être nul (mono)
    void process(float* L, float* R, int frames);
    void process(double* L, double* R, int frames);

    // vu-mètres (mis à jour à la fin de chaque bloc)
    int  meterStep(int i) const { return meterStep_[i]; }      // 0 in L, 1 in R, 2 out L, 3 out R : 0..28 ; 4 dosage : 0..35
    bool clipLed() const { return meterStep_[5] != 0; }
    double meterLevel(int i) const { return meterLin[i]; }     // crête lissée, linéaire

private:
    struct SVF { double C = 0.0, R = 0.0, I = 0.0; };
    struct BandSplit { SVF LP, HP; double G = 0.0, GR = 0.0; };
    static void bandSplitSet(BandSplit& f, double fcL, double fcH, double fs);
    static double besselI0(double x);
    static int meterSegments(double db);

    template <typename T> void processImpl(T* L, T* R, int frames);

    void updateTargets();
    void snapSmoothing();
    void updateCurve(double pct);
    double inflate(double x) const;
    double bandSample(BandSplit& f, double x) const;
    double multiband(BandSplit& f, double x) const;
    void aaInit();
    void aaReset();
    void aaUp(int ch, double x, double& u0, double& u1);
    double aaDown(int ch, double f0, double f1);
    void aaChain(int ch, double x, double& out, double& dpk);
    void updateMeters(int frames);
    void updateAutoGain(int frames);

    // réglages
    double fInput = IN_ZERO_POS, fCurve = 0.5, fEffect = 0.0, fOutput = 1.0;
    bool bClip = true, bSplit = false, bBand = false, bAA = false, bAG = false, bBypass = false;
    double sampleRate = 44100.0;

    // cibles et valeurs lissées
    double tIn = 1.0, tOut = 1.0, tEff = 0.0, tCurve = 0.0;
    double sIn = 1.0, sOut = 1.0, sEff = 0.0, sCurve = 0.0;
    double smoothTime = 0.0, smoothCoef = 0.0;
    double curveA = 1.5, curveB = 0.0, curveC = -0.5, curveD = 0.0625;
    double gLowMax = 1.0, gHighMax = 1.0;

    BandSplit filters[2], filtersOS[2];

    // anti-aliasing x2
    static constexpr int AA_NT = (AA_M + 1) / 2;
    double aaH[AA_NT] = {};
    double aaC = 0.5;
    double upBuf[2][2 * (AA_M + 1)] = {};
    int upPos[2] = {0, 0};
    double dnBuf[2][2 * (2 * AA_M + 1)] = {};
    int dnPos[2] = {0, 0};

    // vu-mètres, dosage, autogain
    double meterLin[5] = {}, meterPeak[5] = {};
    int meterStep_[6] = {};
    bool clipHit = false;
    double clipHold = 0.0;
    double doseInSq = 0.0, doseInMs = 0.0;
    double agSumIn = 0.0, agSumFin = 0.0, agMsIn = 0.0, agMsFin = 0.0;
};

} // namespace mazz
