/*
 * MAZZFLATOR - moteur audio indépendant (version ordinateur)
 * Copyright (C) 2026 Mazztock
 * Dérivé de JS Inflator (Kiriki-liszt), GPL v3. Voir MazzflatorEngine.h et LICENSE.
 */
#include "MazzflatorEngine.h"
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mazz {

// ---------------------------------------------------------------- outils
double MazzflatorEngine::besselI0(double x) {
    double sum = 1.0, term = 1.0, q = x * x / 4.0;
    for (int k = 1; k < 60; k++) { term *= q / ((double)k * (double)k); sum += term; if (term < 1e-18 * sum) break; }
    return sum;
}

void MazzflatorEngine::bandSplitSet(BandSplit& f, double fcL, double fcH, double fs) {
    f.LP.C = 0.5 * tan(M_PI * ((fcL / fs) - 0.25)) + 0.5;
    f.LP.R = f.LP.I = 0.0;
    f.HP.C = 0.5 * tan(M_PI * ((fcH / fs) - 0.25)) + 0.5;
    f.HP.R = f.HP.I = 0.0;
    f.G = f.HP.C * (1.0 - f.LP.C) / (f.HP.C - f.LP.C);
    f.GR = 1.0 / f.G;
}

// Échelle IN/OUT (28 segments) resserrée en haut comme le Sonnox :
// -42 → -10 dB : 8 segments ; -10 → 0 dB : 14 segments ; 0 → +6 dB : 6 segments rouges
int MazzflatorEngine::meterSegments(double db) {
    double f;
    if (db < -42.0) f = 0.0;
    else if (db < -10.0) f = (db + 42.0) / 4.0;
    else if (db < 0.0) f = 8.0 + (db + 10.0) * 1.4;
    else f = 22.0 + db;
    int n = (int)(f + 0.5);
    return n < 0 ? 0 : (n > METER_SEGMENTS ? METER_SEGMENTS : n);
}

double MazzflatorEngine::inputNormToDb(double v) {
    return (v < IN_ZERO_POS) ? IN_MIN_DB * (1.0 - v / IN_ZERO_POS)
                             : IN_MAX_DB * (v - IN_ZERO_POS) / (1.0 - IN_ZERO_POS);
}

// ---------------------------------------------------------------- mise en route
MazzflatorEngine::MazzflatorEngine() {
    gLowMax  = pow(10.0, BAND_LOW_BOOST_DB / 20.0);
    gHighMax = pow(10.0, BAND_HIGH_BOOST_DB / 20.0);
    prepare(44100.0);
}

void MazzflatorEngine::prepare(double sr) {
    sampleRate = (sr > 1000.0) ? sr : 44100.0;
    setSmoothingTime(smoothTime);
    aaInit();
    reset();
}

void MazzflatorEngine::reset() {
    bandSplitSet(filters[0], SPLIT_LOW_HZ, SPLIT_HIGH_HZ, sampleRate);
    bandSplitSet(filters[1], SPLIT_LOW_HZ, SPLIT_HIGH_HZ, sampleRate);
    aaReset();
    for (int i = 0; i < 5; i++) meterLin[i] = meterPeak[i] = 0.0;
    for (int i = 0; i < 6; i++) meterStep_[i] = 0;
    clipHit = false; clipHold = 0.0;
    doseInSq = doseInMs = 0.0;
    agSumIn = agSumFin = agMsIn = agMsFin = 0.0;
    updateTargets();
    snapSmoothing();
}

void MazzflatorEngine::setSmoothingTime(double seconds) {
    smoothTime = seconds > 0.0 ? seconds : 0.0;
    smoothCoef = (smoothTime > 0.0) ? 1.0 - exp(-1.0 / (smoothTime * sampleRate)) : 0.0;
    if (smoothCoef == 0.0) snapSmoothing();
}

// ---------------------------------------------------------------- réglages
static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

void MazzflatorEngine::setInput(double v)  { fInput = clamp01(v);  updateTargets(); }
void MazzflatorEngine::setCurve(double v)  { fCurve = clamp01(v);  updateTargets(); }
void MazzflatorEngine::setEffect(double v) { fEffect = clamp01(v); updateTargets(); }
void MazzflatorEngine::setOutput(double v, bool force) {
    if (bAG && !force) return;              // AUTOGAIN allumé : OUTPUT appartient au moteur
    fOutput = clamp01(v);
    updateTargets();
}
void MazzflatorEngine::setClip(bool on)     { bClip = on; }
void MazzflatorEngine::setSplit(bool on)    { bSplit = on; }
void MazzflatorEngine::setBand(bool on)     { bBand = on; }
void MazzflatorEngine::setAA(bool on)       { if (on != bAA) { bAA = on; aaReset(); } }
void MazzflatorEngine::setAutoGain(bool on) { bAG = on; }
void MazzflatorEngine::setBypass(bool on)   { bBypass = on; }

void MazzflatorEngine::updateTargets() {
    tIn  = exp(log(10.0) * inputNormToDb(fInput) / 20.0);
    tOut = exp(log(10.0) * outputNormToDb(fOutput) / 20.0);
    tEff = fEffect;
    tCurve = (fCurve - 0.5) * CURVE_SCALE;
    if (smoothCoef == 0.0) snapSmoothing();
}

void MazzflatorEngine::snapSmoothing() {
    sIn = tIn; sOut = tOut; sEff = tEff; sCurve = tCurve;
    updateCurve(sCurve);
}

void MazzflatorEngine::updateCurve(double pct) {
    curveA =  1.5 + pct;
    curveB = -(pct + pct);
    curveC =  pct - 0.5;
    curveD = 0.0625 - pct * 0.25 + (pct * pct) * 0.25;
}

// ---------------------------------------------------------------- son
double MazzflatorEngine::inflate(double x) const {
    double sign = (x > 0.0) ? 1.0 : -1.0;
    double s1 = fabs(x);
    if (s1 > 1.0) return sign;              // au-dessus de 0 dBFS : on reste au plafond (saturation douce)
    double s2 = s1 * s1, s3 = s2 * s1, s4 = s2 * s2;
    double y = (curveA * s1) + (curveB * s2) + (curveC * s3) - (curveD * (s2 - (2.0 * s3) + s4));
    return y * sign;
}

// BAND : seule la bande médium (240 Hz - 2,4 kHz) passe dans CLIP et dans l'effet
double MazzflatorEngine::bandSample(BandSplit& f, double x) const {
    f.LP.R = f.LP.I + f.LP.C * (x - f.LP.I);
    f.LP.I = 2.0 * f.LP.R - f.LP.I;
    f.HP.R = (1.0 - f.HP.C) * f.HP.I + f.HP.C * x;
    f.HP.I = 2.0 * f.HP.R - f.HP.I;
    double low = f.LP.R, high = x - f.HP.R, mid = f.HP.R - f.LP.R;
    if (bClip) { if (mid > 1.0) mid = 1.0; else if (mid < -1.0) mid = -1.0; }
    double feed = mid * f.G;
    if (feed > 2.0) feed = 2.0; else if (feed < -2.0) feed = -2.0;
    double wetMid = inflate(feed) * f.GR;
    double gLow  = 1.0 + (gLowMax  - 1.0) * sEff;
    double gHigh = 1.0 + (gHighMax - 1.0) * sEff;
    return low * gLow + high * gHigh + mid + (wetMid - mid) * sEff;
}

// SPLIT : effet appliqué séparément aux 3 bandes
double MazzflatorEngine::multiband(BandSplit& f, double x) const {
    f.LP.R = f.LP.I + f.LP.C * (x - f.LP.I);
    f.LP.I = 2.0 * f.LP.R - f.LP.I;
    f.HP.R = (1.0 - f.HP.C) * f.HP.I + f.HP.C * x;
    f.HP.I = 2.0 * f.HP.R - f.HP.I;
    double lo = f.LP.R, hi = x - f.HP.R, mi = f.HP.R - f.LP.R;
    return inflate(lo) + inflate(mi * f.G) * f.GR + inflate(hi);
}

// ---------------------------------------------------------------- anti-aliasing x2 (demi-bande de Kaiser, phase linéaire)
void MazzflatorEngine::aaInit() {
    double sum = 0.5;
    for (int i = 0; i < AA_NT; i++) {
        int k = 2 * i + 1;
        double ideal = sin(M_PI * k / 2.0) / (M_PI * k);
        double r = (double)k / (double)AA_M;
        aaH[i] = ideal * besselI0(AA_BETA * sqrt(1.0 - r * r)) / besselI0(AA_BETA);
        sum += 2.0 * aaH[i];
    }
    for (int i = 0; i < AA_NT; i++) aaH[i] /= sum;
    aaC = 0.5 / sum;
}

void MazzflatorEngine::aaReset() {
    memset(upBuf, 0, sizeof upBuf);
    memset(dnBuf, 0, sizeof dnBuf);
    upPos[0] = upPos[1] = dnPos[0] = dnPos[1] = 0;
    bandSplitSet(filtersOS[0], SPLIT_LOW_HZ, SPLIT_HIGH_HZ, 2.0 * sampleRate);
    bandSplitSet(filtersOS[1], SPLIT_LOW_HZ, SPLIT_HIGH_HZ, 2.0 * sampleRate);
}

void MazzflatorEngine::aaUp(int ch, double x, double& u0, double& u1) {
    const int R = AA_M + 1;
    int p = upPos[ch];
    p = (p == 0) ? R - 1 : p - 1;
    upPos[ch] = p;
    double* b = upBuf[ch];
    b[p] = x;
    b[p + R] = x;
    const double* h = b + p;
    double acc = 0.0;
    for (int i = 0; i < AA_NT; i++) {
        int o = 2 * i + 1;
        acc += aaH[i] * (h[(AA_M - o) / 2] + h[(AA_M + o) / 2]);
    }
    u0 = 2.0 * acc;
    u1 = 2.0 * aaC * h[(AA_M - 1) / 2];
}

double MazzflatorEngine::aaDown(int ch, double f0, double f1) {
    const int R = 2 * AA_M + 1;
    int p = dnPos[ch];
    double* b = dnBuf[ch];
    p = (p == 0) ? R - 1 : p - 1;
    b[p] = f0; b[p + R] = f0;
    p = (p == 0) ? R - 1 : p - 1;
    b[p] = f1; b[p + R] = f1;
    dnPos[ch] = p;
    const double* h = b + p;
    double acc = aaC * h[AA_M];
    for (int i = 0; i < AA_NT; i++) {
        int o = 2 * i + 1;
        acc += aaH[i] * (h[AA_M - o] + h[AA_M + o]);
    }
    return acc;
}

void MazzflatorEngine::aaChain(int ch, double x, double& out, double& dpk) {
    double u[2], f[2];
    aaUp(ch, x, u[0], u[1]);
    for (int k = 0; k < 2; k++) {
        double s = u[k], fin;
        if (bBand) {
            fin = bandSample(filtersOS[ch], s);
        } else {
            if (bClip) { if (s > 1.0) s = 1.0; else if (s < -1.0) s = -1.0; }
            if (s > 2.0) s = 2.0; else if (s < -2.0) s = -2.0;
            double wet = bSplit ? multiband(filtersOS[ch], s) : inflate(s);
            fin = s + (wet - s) * sEff;
        }
        f[k] = fin;
        double d = fabs(fin - s);
        if (d > dpk) dpk = d;
    }
    out = aaDown(ch, f[0], f[1]);
}

// ---------------------------------------------------------------- vu-mètres et AUTOGAIN (fin de bloc)
void MazzflatorEngine::updateMeters(int frames) {
    double fr = (double)(frames > 0 ? frames : 1);
    double rel = exp(-(double)frames / (sampleRate * METER_RELEASE_S));
    for (int i = 0; i < 4; i++) {
        double p = meterPeak[i];
        meterPeak[i] = 0.0;
        if (p > meterLin[i]) meterLin[i] = p; else meterLin[i] *= rel;
        double db = (meterLin[i] > 1e-6) ? 20.0 * log10(meterLin[i]) : -120.0;
        meterStep_[i] = meterSegments(db);
    }
    // dosage : crête du signal AJOUTÉ par l'effet / niveau moyen d'entrée sur ~2 s, -24 .. +12 dB
    doseInMs += (doseInSq / fr - doseInMs) * (1.0 - exp(-fr / (sampleRate * 2.0)));
    doseInSq = 0.0;
    double p4 = meterPeak[4];
    meterPeak[4] = 0.0;
    double rel4 = exp(-fr / (sampleRate * DOSE_RELEASE_S));
    if (p4 > meterLin[4]) meterLin[4] = p4; else meterLin[4] *= rel4;
    double nr;
    if (fEffect <= 0.001 || doseInMs < 1e-10) {
        meterLin[4] = 0.0;
        nr = 0.0;
    } else {
        double db4 = (meterLin[4] > 1e-9) ? 20.0 * log10(meterLin[4] / sqrt(doseInMs)) : -140.0;
        nr = (db4 + 24.0) / 36.0;
    }
    if (nr < 0.0) nr = 0.0; else if (nr > 1.0) nr = 1.0;
    meterStep_[4] = (int)(nr * DOSE_STEPS + 0.5);
    // diode CLIP d'entrée : tenue 2 s après le dernier échantillon à 0 dBFS
    if (clipHit) { clipHold = CLIP_HOLD_S; clipHit = false; }
    else if (clipHold > 0.0) clipHold -= (double)frames / sampleRate;
    meterStep_[5] = (clipHold > 0.0) ? 1 : 0;
}

void MazzflatorEngine::updateAutoGain(int frames) {
    double fr = (double)(frames > 0 ? frames : 1);
    double a = 1.0 - exp(-fr / (sampleRate * AG_TAU));
    agMsIn  += (agSumIn  / fr - agMsIn)  * a;
    agMsFin += (agSumFin / fr - agMsFin) * a;
    agSumIn = agSumFin = 0.0;
    if (!bAG) return;
    if (agMsIn < 1e-7 || agMsFin < 1e-9) return;          // silence : on garde le réglage actuel
    double targetDb = 10.0 * log10(agMsIn / agMsFin);
    if (targetDb < -OUT_RANGE_DB) targetDb = -OUT_RANGE_DB; else if (targetDb > 0.0) targetDb = 0.0;
    double curDb = outputNormToDb(fOutput);
    double maxStep = AG_SLEW_DB_PER_S * fr / sampleRate;
    double d = targetDb - curDb;
    if (d > maxStep) d = maxStep; else if (d < -maxStep) d = -maxStep;
    curDb += d;
    fOutput = (curDb + OUT_RANGE_DB) / OUT_RANGE_DB;
    updateTargets();
}

// ---------------------------------------------------------------- traitement d'un bloc
void MazzflatorEngine::process(float* L, float* R, int frames)  { processImpl(L, R, frames); }
void MazzflatorEngine::process(double* L, double* R, int frames) { processImpl(L, R, frames); }

template <typename T>
void MazzflatorEngine::processImpl(T* L, T* R, int frames) {
    if (!L || frames <= 0) return;
    const bool stereo = (R != nullptr);

    if (bBypass) {          // son intact, les vu-mètres continuent de bouger
        for (int i = 0; i < frames; i++) {
            double l = fabs((double)L[i]), r = stereo ? fabs((double)R[i]) : l;
            if (l > meterPeak[0]) meterPeak[0] = l;
            if (r > meterPeak[1]) meterPeak[1] = r;
            if (l > meterPeak[2]) meterPeak[2] = l;
            if (r > meterPeak[3]) meterPeak[3] = r;
        }
        updateMeters(frames);
        return;
    }

    const int nch = stereo ? 2 : 1;
    T* io[2] = {L, R};
    for (int i = 0; i < frames; i++) {
        if (smoothCoef > 0.0) {                 // lissage des réglages, échantillon par échantillon
            sIn  += (tIn  - sIn)  * smoothCoef;
            sOut += (tOut - sOut) * smoothCoef;
            sEff += (tEff - sEff) * smoothCoef;
            const double dc = tCurve - sCurve;
            if (std::abs(dc) > 1e-9) { sCurve += dc * smoothCoef; updateCurve(sCurve); }
            else if (std::abs(dc) > 0.0) { sCurve = tCurve; updateCurve(sCurve); }
        }
        double raw[2], s[2], fin[2], dosePk = 0.0;
        for (int c = 0; c < nch; c++) raw[c] = (double)io[c][i];
        if (!stereo) raw[1] = raw[0];
        for (int c = 0; c < 2; c++) s[c] = raw[c] * sIn;

        // vu-mètre d'entrée (comme le Sonnox) : avec CLIP jamais au-dessus de 0 dB, sinon jusqu'à +6 dB
        const double lim = bClip ? 1.0 : 2.0;
        for (int c = 0; c < 2; c++) {
            double m = fabs(s[c]) < lim ? fabs(s[c]) : lim;
            if (m > meterPeak[c]) meterPeak[c] = m;
            if (m >= 0.9999) clipHit = true;
        }
        doseInSq += 0.5 * (s[0] * s[0] + s[1] * s[1]);

        for (int c = 0; c < nch; c++) {
            if (bAA) {
                aaChain(c, s[c], fin[c], dosePk);
            } else if (bBand) {
                fin[c] = bandSample(filters[c], s[c]);
                double d = fabs(fin[c] - s[c]); if (d > dosePk) dosePk = d;
            } else {
                double x = s[c];
                if (bClip) { if (x > 1.0) x = 1.0; else if (x < -1.0) x = -1.0; }
                if (x > 2.0) x = 2.0; else if (x < -2.0) x = -2.0;
                double wet = bSplit ? multiband(filters[c], x) : inflate(x);
                fin[c] = x + (wet - x) * sEff;
                double d = fabs(fin[c] - x); if (d > dosePk) dosePk = d;
            }
        }
        if (!stereo) fin[1] = fin[0];

        agSumIn  += 0.5 * (raw[0] * raw[0] + raw[1] * raw[1]);
        agSumFin += 0.5 * (fin[0] * fin[0] + fin[1] * fin[1]);
        if (dosePk > meterPeak[4]) meterPeak[4] = dosePk;

        for (int c = 0; c < 2; c++) {
            double o = fin[c] * sOut;
            double a = fabs(o);
            if (a > meterPeak[2 + c]) meterPeak[2 + c] = a;
            if (c < nch) io[c][i] = (T)o;
        }
    }
    updateAutoGain(frames);
    updateMeters(frames);
}

template void MazzflatorEngine::processImpl<float>(float*, float*, int);
template void MazzflatorEngine::processImpl<double>(double*, double*, int);

} // namespace mazz
