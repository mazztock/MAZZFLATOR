// MAZZFLATOR - moteur pour MPC OS (mpc-vst-plugins SDK)
// Copyright (C) 2026 Mazztock. Basé sur JS Inflator de Kiriki-liszt (https://github.com/Kiriki-liszt/JS_Inflator).
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see the LICENSE file.

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
    #include "engine.h"
}

inline double int16_to_double(int16_t s) { return s / 32768.0; }
inline int16_t double_to_int16(double s) {
    if (s > 1.0) s = 1.0;
    else if (s < -1.0) s = -1.0;
    return static_cast<int16_t>(s * 32767.0);
}

// Réglages enregistrés dans le projet (la MPC les demande via la clé "state", le wrapper les range dans un "chunk").
// Les vu-mètres (in_l, in_r, out_l, out_r, fx_dose) n'en font pas partie : ils sont recalculés en direct.
#ifndef AA_M
#define AA_M 23          // demi-longueur du filtre demi-bande à 88,2 kHz ; latence totale = AA_M échantillons à 44,1 kHz
#endif
#ifndef AA_BETA
#define AA_BETA 8.0      // paramètre de la fenêtre de Kaiser
#endif
// plage du fader INPUT (gain d'entrée) : de IN_MIN_DB (en bas) à IN_MAX_DB (en haut), en deux pentes :
// 0 dB à IN_ZERO_POS (40 % de la course) ; de -6 à 0 dB la même longueur que de 0 à +12 dB
// graduations régulières : -6, -3, 0, +6, +12, +18 tous les 20 %
#define IN_MIN_DB -6.0
#define IN_MAX_DB 18.0
#define IN_ZERO_POS 0.4
#define CURVE_SCALE 1.0   // 1.0 = -50..+50 comme le Sonnox et l'Inflator JS (2.0 = -100..+100)
#define CLIP_HOLD_S 2.0       // tenue de la diode CLIP d'entrée (s), comme le Sonnox (2 s / 5 s)
#define DOSE_RELEASE_S 0.07   // retombée du dosage (s) : comportement crête, vivant
#ifndef OUT_RANGE_DB
#define OUT_RANGE_DB 24.0   // plage du fader OUTPUT : de 0 dB (en haut) à -OUT_RANGE_DB (en bas)
#endif
#define NPERSIST 10
static const char *const PERSIST_KEYS[NPERSIST] = {"input", "curve", "effect", "output", "clip", "bandsplit", "bypass", "band", "aa", "ag"};

static double bessel_i0(double x) {
    double sum = 1.0, term = 1.0, q = x * x / 4.0;
    for (int k = 1; k < 60; k++) { term *= q / ((double)k * (double)k); sum += term; if (term < 1e-18 * sum) break; }
    return sum;
}

struct SVF {
    double C = 0.0;
    double R = 0.0;
    double I = 0.0;
};

struct Band_Split {
    SVF LP;
    SVF HP;
    double G = 0.0;
    double GR = 0.0;
    double SR = 0.0;
};

static void Band_Split_set(Band_Split* filter, double Fc_L, double Fc_H, double Fs) {
    filter->SR = Fs;
    filter->LP.C = 0.5 * tan(M_PI * ((Fc_L / Fs) - 0.25)) + 0.5;
    filter->LP.R = 0.0;
    filter->LP.I = 0.0;
    filter->HP.C = 0.5 * tan(M_PI * ((Fc_H / Fs) - 0.25)) + 0.5;
    filter->HP.R = 0.0;
    filter->HP.I = 0.0;
    filter->G = filter->HP.C * (1.0 - filter->LP.C) / (filter->HP.C - filter->LP.C);
    filter->GR = 1.0 / filter->G;
}

struct JS_Inflator_Engine {
    double fInput = IN_ZERO_POS;   // 0 dB
    double fEffect = 0.0;
    double fCurve = 0.5;
    double fOutput = 1.0;
    int bClip = 1;                                     // CLIP 0dB allumé par défaut, comme le Sonnox
    int bSplit = 0;
    int bAutoLink = 0;
    // BAND : gain ajouté aux bandes qui ne passent pas dans l'effet (à EFFECT 100 % ; proportionnel à EFFECT, donc nul à 0)
    static constexpr double BAND_LOW_BOOST_DB  = 3.0;   // graves < 240 Hz
    static constexpr double BAND_HIGH_BOOST_DB = 2.0;   // aigus  > 2,4 kHz
    // --- AUTOGAIN : le moteur règle OUTPUT pour que le niveau de sortie (RMS) rejoigne celui de l'entrée du plugin ---
    static constexpr double AG_TAU = 1.5;                  // lissage de la mesure, en secondes
    static constexpr double AG_SLEW_DB_PER_S = 4.0;        // vitesse maximale de variation d'OUTPUT
    int bAG = 0;
    int restoring = 0;                                     // 1 pendant la relecture d'un état enregistré
    double agSumIn = 0.0, agSumFin = 0.0;                  // somme des carrés du bloc en cours (entrée du plugin / avant le gain de sortie)
    double agMsIn = 0.0, agMsFin = 0.0;                    // énergies moyennes lissées
    double agReported = 1.0;                               // dernière valeur d'OUTPUT signalée à la MPC
    int bBand = 0;      // mode BAND : seuls les médiums (240 Hz - 2,4 kHz) sont traités, graves et aigus restent intacts
    int bBypass = 0;
    double sampleRate = 44100.0;

    double curvepct;
    double curveA;
    double curveB;
    double curveC;
    double curveD;
    double In_db;
    double Out_db;

    // Corrected: Tableau de deux filtres pour la stéréo gauche/droite
    Band_Split filters[2];

    // --- Anti-aliasing : suréchantillonnage x2 (filtre demi-bande de Kaiser, phase linéaire) ---
    Band_Split filtersOS[2];                   // mêmes séparateurs (240 Hz / 2,4 kHz) calculés pour 88,2 kHz
    int bAA = 0;
    static const int AA_NT = (AA_M + 1) / 2;   // coefficients impairs non nuls d'un côté
    double aaH[AA_NT];                         // h[1], h[3], ..., h[AA_M] (gain continu 1 à 88,2 kHz)
    double aaC = 0.5;                          // coefficient central
    double upBuf[2][2 * (AA_M + 1)];           // registre circulaire (écriture double) : entrées passées, cadence 44,1 kHz
    int    upPos[2] = {0, 0};
    double dnBuf[2][2 * (2 * AA_M + 1)];       // registre circulaire (écriture double) : sorties passées, cadence 88,2 kHz
    int    dnPos[2] = {0, 0};

    void aa_reset() {
        memset(upBuf, 0, sizeof upBuf);
        memset(dnBuf, 0, sizeof dnBuf);
        upPos[0] = upPos[1] = dnPos[0] = dnPos[1] = 0;
        Band_Split_set(&filtersOS[0], 240.0, 2400.0, 88200.0);
        Band_Split_set(&filtersOS[1], 240.0, 2400.0, 88200.0);
    }
    void aa_init() {
        double sum = 0.5;
        for (int i = 0; i < AA_NT; i++) {
            int k = 2 * i + 1;
            double ideal = sin(M_PI * k / 2.0) / (M_PI * k);
            double r = (double)k / (double)AA_M;
            aaH[i] = ideal * bessel_i0(AA_BETA * sqrt(1.0 - r * r)) / bessel_i0(AA_BETA);
            sum += 2.0 * aaH[i];
        }
        for (int i = 0; i < AA_NT; i++) aaH[i] /= sum;
        aaC = 0.5 / sum;
        aa_reset();
    }
    void aa_up(int ch, double x, double &u0, double &u1) {
        const int R = AA_M + 1;
        int p = upPos[ch];
        p = (p == 0) ? R - 1 : p - 1;
        upPos[ch] = p;
        double *b = upBuf[ch];
        b[p] = x;
        b[p + R] = x;
        const double *h = b + p;                   // h[j] = x[n-j]
        double acc = 0.0;
        for (int i = 0; i < AA_NT; i++) {
            int o = 2 * i + 1;
            acc += aaH[i] * (h[(AA_M - o) / 2] + h[(AA_M + o) / 2]);
        }
        u0 = 2.0 * acc;
        u1 = 2.0 * aaC * h[(AA_M - 1) / 2];
    }
    double aa_down(int ch, double f0, double f1) {
        const int R = 2 * AA_M + 1;
        int p = dnPos[ch];
        double *b = dnBuf[ch];
        p = (p == 0) ? R - 1 : p - 1;
        b[p] = f0;
        b[p + R] = f0;
        p = (p == 0) ? R - 1 : p - 1;
        b[p] = f1;
        b[p + R] = f1;
        dnPos[ch] = p;
        const double *h = b + p;                   // h[0] = f1 (le plus récent), h[1] = f0
        double acc = aaC * h[AA_M];
        for (int i = 0; i < AA_NT; i++) {
            int o = 2 * i + 1;
            acc += aaH[i] * (h[AA_M - o] + h[AA_M + o]);
        }
        return acc;
    }

    // --- Vu-mètres : in_l, in_r, out_l, out_r + fx_dose (valeurs 0..1 en 36 crans, publiées via mpc_engine_changed) ---
    // fx_dose = crête du signal AJOUTÉ par l'effet, relative au niveau moyen d'entrée : -24 dB .. +12 dB
    double meterLin[5]  = {0.0, 0.0, 0.0, 0.0, 0.0};   // crête lissée (linéaire)
    double meterPeak[5] = {0.0, 0.0, 0.0, 0.0, 0.0};   // crête du bloc audio en cours
    // niveau affiché : in/out = nombre de segments allumés 0..28 ; fx_dose 0..35 ; [5] = diode CLIP d'entrée 0/1
    int meterStep[6]    = {0, 0, 0, 0, 0, 0};
    int meterSent[6]    = {0, 0, 0, 0, 0, 0};             // dernier niveau signalé à la MPC
    int clipHit = 0;                                   // un échantillon à pleine échelle (0 dBFS) dans le bloc
    double clipHold = 0.0;                             // temps restant d'allumage de la diode CLIP (s)
    int meterScan = 0;
    int persistDirty[NPERSIST] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // réglages restaurés à signaler à la MPC pour que l'écran les affiche
    double doseInSq = 0.0;                            // somme des carrés du signal d'entrée (après INPUT), bloc en cours
    double doseInMs = 0.0;                            // son énergie moyenne lissée

    // Échelle des vu-mètres IN/OUT (28 segments), resserrée en haut comme le Sonnox :
    //   -42 → -10 dB : 8 segments (4 dB) ; -10 → 0 dB : 14 segments (~0,7 dB) ; 0 → +6 dB : 6 segments (1 dB, surcharge)
    static int meter_segments(double db) {
        double f;
        if (db < -42.0) f = 0.0;
        else if (db < -10.0) f = (db + 42.0) / 4.0;
        else if (db < 0.0) f = 8.0 + (db + 10.0) * 1.4;
        else f = 22.0 + db;
        int n = (int)(f + 0.5);
        return n < 0 ? 0 : (n > 28 ? 28 : n);
    }

    void update_meters(int frames) {
        // retombée ~300 ms ; échelle -42 dB .. +6 dB (comme le Sonnox Inflator)
        double rel = exp(-(double)frames / (44100.0 * 0.3));
        for (int i = 0; i < 5; i++) {
            if (i == 4) {                              // dosage de l'effet = signal AJOUTÉ par l'effet seul (traité - original)
                // dosage VIVANT : crête du signal ajouté par l'effet (attaque immédiate, retombée rapide DOSE_RELEASE_S),
                // comparée au niveau moyen de l'entrée sur ~2 s. Il danse avec les transitoires de la musique,
                // mais monter INPUT ne le décale pas (tout est relatif à l'entrée). EFFECT à 0 = dosage à 0.
                // échelle -24 dB .. +12 dB (crête ajoutée / RMS d'entrée)
                double nr;
                double fr = (double)(frames > 0 ? frames : 1);
                doseInMs += (doseInSq / fr - doseInMs) * (1.0 - exp(-fr / (44100.0 * 2.0)));
                doseInSq = 0.0;
                double p4 = meterPeak[4];
                meterPeak[4] = 0.0;
                double rel4 = exp(-fr / (44100.0 * DOSE_RELEASE_S));
                if (p4 > meterLin[4]) meterLin[4] = p4; else meterLin[4] *= rel4;
                if (fEffect <= 0.001 || doseInMs < 1e-10) {
                    meterLin[4] = 0.0;
                    nr = 0.0;
                } else {
                    double db4 = (meterLin[4] > 1e-9) ? 20.0 * log10(meterLin[4] / sqrt(doseInMs)) : -140.0;
                    nr = (db4 + 24.0) / 36.0;
                }
                if (nr < 0.0) nr = 0.0; else if (nr > 1.0) nr = 1.0;
                meterStep[4] = (int)(nr * 35.0 + 0.5);
                continue;
            }
            double p = meterPeak[i];
            meterPeak[i] = 0.0;
            if (p > meterLin[i]) meterLin[i] = p; else meterLin[i] *= rel;
            double db = (meterLin[i] > 1e-6) ? 20.0 * log10(meterLin[i]) : -120.0;
            meterStep[i] = meter_segments(db);
        }
        // diode CLIP d'entrée (comme le voyant « 0 dB over » du Sonnox) : tenue 2 s après le dernier échantillon à 0 dBFS
        if (clipHit) { clipHold = CLIP_HOLD_S; clipHit = 0; }
        else if (clipHold > 0.0) clipHold -= (double)frames / 44100.0;
        meterStep[5] = (clipHold > 0.0) ? 1 : 0;
    }

    void init_filters(double sr) {
        sampleRate = sr;
        Band_Split_set(&filters[0], 240.0, 2400.0, sampleRate);
        Band_Split_set(&filters[1], 240.0, 2400.0, sampleRate);
    }

    // Un canal en mode AA : x2 -> écrêtage / effet à 88,2 kHz -> retour à 44,1 kHz. dsq/dpk : mesure du dosage.
    void aa_chain(int ch, double x, double &out, double &dsq, double &dpk) {
        double u[2], f[2];
        aa_up(ch, x, u[0], u[1]);
        for (int k = 0; k < 2; k++) {
            double s = u[k], fin;
            if (bBand) {
                fin = band_sample_f(&filtersOS[ch], s);
            } else {
                if (bClip) { if (s > 1.0) s = 1.0; else if (s < -1.0) s = -1.0; }
                if (s > 2.0) s = 2.0; else if (s < -2.0) s = -2.0;
                double wet = bSplit ? multiband_f(&filtersOS[ch], s) : process_inflator(s);
                fin = s + (wet - s) * fEffect;
            }
            f[k] = fin;
            double d = fin - s;
            dsq += d * d;
            if (fabs(d) > dpk) dpk = fabs(d);
        }
        out = aa_down(ch, f[0], f[1]);
    }

    void update_autogain(int frames) {
        double fr = (double)(frames > 0 ? frames : 1);
        double a = 1.0 - exp(-fr / (44100.0 * AG_TAU));
        agMsIn  += (agSumIn  / fr - agMsIn)  * a;
        agMsFin += (agSumFin / fr - agMsFin) * a;
        agSumIn = agSumFin = 0.0;
        if (!bAG) return;
        if (agMsIn < 1e-7 || agMsFin < 1e-9) return;       // silence : on garde le réglage actuel
        double targetDb = 10.0 * log10(agMsIn / agMsFin);  // gain de sortie qui remet le niveau d'entrée
        if (targetDb < -OUT_RANGE_DB) targetDb = -OUT_RANGE_DB; else if (targetDb > 0.0) targetDb = 0.0;
        double curDb = OUT_RANGE_DB * fOutput - OUT_RANGE_DB;
        double maxStep = AG_SLEW_DB_PER_S * fr / 44100.0;
        double d = targetDb - curDb;
        if (d > maxStep) d = maxStep; else if (d < -maxStep) d = -maxStep;
        curDb += d;
        fOutput = (curDb + OUT_RANGE_DB) / OUT_RANGE_DB;
        update_coefficients();
        if (fabs(fOutput - agReported) >= 1.0 / 70.0) {    // assez de chemin parcouru : la MPC redessine le fader
            agReported = fOutput;
            persistDirty[3] = 1;                           // 3 = OUTPUT dans PERSIST_KEYS
        }
    }

    void update_coefficients() {
        double inDb = (fInput < IN_ZERO_POS) ? IN_MIN_DB * (1.0 - fInput / IN_ZERO_POS)
                                             : IN_MAX_DB * (fInput - IN_ZERO_POS) / (1.0 - IN_ZERO_POS);
        In_db  = exp(log(10.0) * inDb / 20.0);

        if (bAutoLink) {                               // (non utilisé) OUTPUT = -INPUT, dans la plage d'OUTPUT
            double o = -inDb;
            if (o > 0.0) o = 0.0; else if (o < -OUT_RANGE_DB) o = -OUT_RANGE_DB;
            fOutput = (o + OUT_RANGE_DB) / OUT_RANGE_DB;
            Out_db = exp(log(10.0) * o / 20.0);
        } else {
            Out_db = exp(log(10.0) * (OUT_RANGE_DB * fOutput - OUT_RANGE_DB) / 20.0);
        }

        curvepct = (fCurve - 0.5) * CURVE_SCALE;   // CURVE_SCALE 2 : -100 .. +100 (l'original JS va de -50 à +50)
        curveA =  1.5 + curvepct; 
        curveB = -(curvepct + curvepct); 
        curveC =  curvepct - 0.5; 
        curveD = 0.0625 - curvepct * 0.25 + (curvepct * curvepct) * 0.25;
    }

    double process_inflator(double inputSample) {
        double sign = (inputSample > 0.0) ? 1.0 : -1.0;
        double s1 = fabs(inputSample);
        double s2 = s1 * s1;
        double s3 = s2 * s1;
        double s4 = s2 * s2;

        if (s1 > 1.0) {
            // au-dessus de 0 dBFS : la courbe arrive à 1 avec une pente nulle, on reste donc au plafond
            // (saturation douce, comme le Sonnox) au lieu du repli 2s - s² de l'Inflator JS qui retombait à 0 à +6 dB
            inputSample = 1.0;
        } else {
            inputSample = (curveA * s1) +
                          (curveB * s2) +
                          (curveC * s3) -
                          (curveD * (s2 - (2.0 * s3) + s4));
        }
        return inputSample * sign;
    }

    // BAND : même découpage en 3 bandes que le Split. Seule la bande médium passe dans l'écrêtage (CLIP) et dans
    // l'effet, avec le dosage EFFECT ; les graves et les aigus rejoignent la sortie tels quels (ni écrêtés, ni traités).
    double band_sample(double x, int channel) { return band_sample_f(&filters[channel], x); }

    double band_sample_f(Band_Split* flt, double x) {

        flt->LP.R = flt->LP.I + flt->LP.C * (x - flt->LP.I);
        flt->LP.I = 2.0 * flt->LP.R - flt->LP.I;

        flt->HP.R = (1.0 - flt->HP.C) * flt->HP.I + flt->HP.C * x;
        flt->HP.I = 2.0 * flt->HP.R - flt->HP.I;

        double low  = flt->LP.R;
        double high = x - flt->HP.R;
        double mid  = flt->HP.R - flt->LP.R;

        if (bClip) {                       // écrêtage de la bande médium seule
            if (mid >  1.0) mid =  1.0;
            else if (mid < -1.0) mid = -1.0;
        }
        double feed = mid * flt->G;        // même mise à niveau de la bande que dans le mode Split
        if (feed >  2.0) feed =  2.0;
        else if (feed < -2.0) feed = -2.0;
        double wetMid = process_inflator(feed) * flt->GR;

        double gLow  = 1.0 + (pow(10.0, BAND_LOW_BOOST_DB  / 20.0) - 1.0) * fEffect;
        double gHigh = 1.0 + (pow(10.0, BAND_HIGH_BOOST_DB / 20.0) - 1.0) * fEffect;
        return low * gLow + high * gHigh + mid + (wetMid - mid) * fEffect;
    }

    double process_sample_multiband(double sampleOS, int channel) { return multiband_f(&filters[channel], sampleOS); }

    double multiband_f(Band_Split* flt, double sampleOS) {

        flt->LP.R = flt->LP.I + flt->LP.C * (sampleOS - flt->LP.I);
        flt->LP.I = 2.0 * flt->LP.R - flt->LP.I;

        flt->HP.R = (1.0 - flt->HP.C) * flt->HP.I + flt->HP.C * sampleOS;
        flt->HP.I = 2.0 * flt->HP.R - flt->HP.I;

        double inputSample_L = flt->LP.R;
        double inputSample_H = sampleOS - flt->HP.R;
        double inputSample_M = flt->HP.R - flt->LP.R;

        return process_inflator(inputSample_L) +
               process_inflator(inputSample_M * flt->G) * flt->GR +
               process_inflator(inputSample_H);
    }
};

static void* mpc_wrapper_create(const char *data_dir) {
    JS_Inflator_Engine* engine = new JS_Inflator_Engine();
    engine->init_filters(44100.0);
    engine->aa_init();
    engine->update_coefficients();
    return engine;
}

static void mpc_wrapper_destroy(void* inst) {
    delete static_cast<JS_Inflator_Engine*>(inst);
}

static void mpc_wrapper_midi(void* inst, const uint8_t *msg, int len) {}

static void mpc_wrapper_set_param(void* inst, const char *key, const char *val);

// Relit un état enregistré ("clé=valeur;clé=valeur;...") : seules les 8 clés connues sont acceptées, valeurs bornées.
static void mpc_wrapper_restore_state(void* inst, const char *str) {
    JS_Inflator_Engine* e = static_cast<JS_Inflator_Engine*>(inst);
    if (!str) return;
    char buf[1024];
    strncpy(buf, str, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    e->restoring = 1;
    char *p = buf;
    while (*p) {
        char *end = strchr(p, ';');
        if (end) *end = 0;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            for (int i = 0; i < NPERSIST; i++) {
                if (strcmp(p, PERSIST_KEYS[i]) != 0) continue;
                double d = atof(eq + 1);
                if (d < 0.0) d = 0.0; else if (d > 1.0) d = 1.0;
                char tmp[32];
                snprintf(tmp, sizeof tmp, "%.6f", d);
                mpc_wrapper_set_param(inst, PERSIST_KEYS[i], tmp);
                e->persistDirty[i] = 1;
                break;
            }
        }
        if (!end) break;
        p = end + 1;
    }
    e->restoring = 0;
}

static void mpc_wrapper_set_param(void* inst, const char *key, const char *val) {
    JS_Inflator_Engine* engine = static_cast<JS_Inflator_Engine*>(inst);
    if (strcmp(key, "state") == 0) { mpc_wrapper_restore_state(inst, val); return; }
    double value = atof(val);

    // vu-mètres : pilotés par le moteur, jamais réglables de l'extérieur
    if (strcmp(key, "in_l") == 0 || strcmp(key, "in_r") == 0 ||
        strcmp(key, "out_l") == 0 || strcmp(key, "out_r") == 0 || strcmp(key, "fx_dose") == 0 ||
        strcmp(key, "in_clip") == 0) return;

    if (strcmp(key, "input") == 0)          engine->fInput = value;
    else if (strcmp(key, "effect") == 0)     engine->fEffect = value;
    else if (strcmp(key, "curve") == 0)      engine->fCurve = value;
    else if (strcmp(key, "output") == 0) {
        if (engine->bAG && !engine->restoring) {           // AUTOGAIN allumé : OUTPUT appartient au moteur
            if (fabs(value - engine->fOutput) > 0.01) engine->persistDirty[3] = 1;   // le fader reprend sa place
            return;
        }
        engine->fOutput = value;
        engine->agReported = value;
    }
    else if (strcmp(key, "clip") == 0)       engine->bClip = (value > 0.5) ? 1 : 0;
    else if (strcmp(key, "bandsplit") == 0)  engine->bSplit = (value > 0.5) ? 1 : 0;
    else if (strcmp(key, "autolink") == 0)   engine->bAutoLink = (value > 0.5) ? 1 : 0;
    else if (strcmp(key, "band") == 0)       engine->bBand = (value > 0.5) ? 1 : 0;
    else if (strcmp(key, "aa") == 0)         { int v = (value > 0.5) ? 1 : 0; if (v != engine->bAA) { engine->bAA = v; engine->aa_reset(); } }
    else if (strcmp(key, "ag") == 0)         engine->bAG = (value > 0.5) ? 1 : 0;
    else if (strcmp(key, "bypass") == 0)     engine->bBypass = (value > 0.5) ? 1 : 0;

    engine->update_coefficients();
}

static int mpc_wrapper_get_param(void* inst, const char *key, char *buf, int buf_len) {
    JS_Inflator_Engine* engine = static_cast<JS_Inflator_Engine*>(inst);
    double val = 0.0;

    if (strcmp(key, "ag_db") == 0) {          // texte affiché à l'écran : niveau d'OUTPUT en dB (0,0 en haut du fader)
        double db = OUT_RANGE_DB * engine->fOutput - OUT_RANGE_DB;
        if (db > -0.05) db = 0.0;
        int n = snprintf(buf, buf_len, "%.1f dB", db);
        return (n > 0 && n < buf_len) ? 1 : 0;
    }

    if (strcmp(key, "state") == 0) {          // état à enregistrer avec le projet
        int n = snprintf(buf, buf_len, "input=%.6f;curve=%.6f;effect=%.6f;output=%.6f;clip=%d;bandsplit=%d;bypass=%d;band=%d;aa=%d;ag=%d;",
                         engine->fInput, engine->fCurve, engine->fEffect, engine->fOutput,
                         engine->bClip, engine->bSplit, engine->bBypass, engine->bBand, engine->bAA, engine->bAG);
        return (n > 0 && n < buf_len) ? 1 : 0;
    }

    if (strcmp(key, "input") == 0)          val = engine->fInput;
    else if (strcmp(key, "effect") == 0)     val = engine->fEffect;
    else if (strcmp(key, "curve") == 0)      val = engine->fCurve;
    else if (strcmp(key, "output") == 0)     val = engine->fOutput;
    else if (strcmp(key, "clip") == 0)       val = engine->bClip;
    else if (strcmp(key, "bandsplit") == 0)  val = engine->bSplit;
    else if (strcmp(key, "autolink") == 0)   val = engine->bAutoLink;
    else if (strcmp(key, "band") == 0)       val = engine->bBand;
    else if (strcmp(key, "aa") == 0)         val = engine->bAA;
    else if (strcmp(key, "ag") == 0)         val = engine->bAG;
    else if (strcmp(key, "bypass") == 0)     val = engine->bBypass;
    else if (strcmp(key, "in_l") == 0)       val = engine->meterStep[0] / 28.0;
    else if (strcmp(key, "in_r") == 0)       val = engine->meterStep[1] / 28.0;
    else if (strcmp(key, "out_l") == 0)      val = engine->meterStep[2] / 28.0;
    else if (strcmp(key, "out_r") == 0)      val = engine->meterStep[3] / 28.0;
    else if (strcmp(key, "fx_dose") == 0)    val = engine->meterStep[4] / 35.0;
    else if (strcmp(key, "in_clip") == 0)    val = engine->meterStep[5];
    else return 0;

    snprintf(buf, buf_len, "%f", val);
    return 1;
}

static void mpc_wrapper_process(void* inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    JS_Inflator_Engine* engine = static_cast<JS_Inflator_Engine*>(inst);

    if (engine->bBypass) {
        for (int i = 0; i < frames * 2; i += 2) {
            double l = fabs(int16_to_double(in_lr[i])), r = fabs(int16_to_double(in_lr[i+1]));
            if (l > engine->meterPeak[0]) engine->meterPeak[0] = l;
            if (r > engine->meterPeak[1]) engine->meterPeak[1] = r;
            if (l > engine->meterPeak[2]) engine->meterPeak[2] = l;
            if (r > engine->meterPeak[3]) engine->meterPeak[3] = r;
        }
        engine->update_meters(frames);
        if (in_lr != out_lr) {
            memcpy(out_lr, in_lr, frames * 2 * sizeof(int16_t));
        }
        return;
    }

    for (int i = 0; i < frames * 2; i += 2) {
        double rawL = int16_to_double(in_lr[i]);
        double rawR = int16_to_double(in_lr[i+1]);
        double sampleL = rawL * engine->In_db;
        double sampleR = rawR * engine->In_db;

        // vu-mètres d'entrée : après le gain d'entrée
        {   // vu-mètre d'entrée comme le Sonnox : CLIP 0dB → jamais au-dessus de 0 dB ; sinon jusqu'à +6 dB (écrêtage dur)
            double lim = engine->bClip ? 1.0 : 2.0;
            double mL = fabs(sampleL) < lim ? fabs(sampleL) : lim;
            double mR = fabs(sampleR) < lim ? fabs(sampleR) : lim;
            if (mL > engine->meterPeak[0]) engine->meterPeak[0] = mL;
            if (mR > engine->meterPeak[1]) engine->meterPeak[1] = mR;
            if (mL >= 0.9999 || mR >= 0.9999) engine->clipHit = 1;   // échantillon à pleine échelle
        }
        engine->doseInSq += 0.5 * (sampleL * sampleL + sampleR * sampleR);   // référence du dosage relatif

        double finalL, finalR, dosePk;
        if (engine->bAA) {
            double sqL = 0.0, pkL = 0.0, sqR = 0.0, pkR = 0.0;
            engine->aa_chain(0, sampleL, finalL, sqL, pkL);
            engine->aa_chain(1, sampleR, finalR, sqR, pkR);
            dosePk = pkL > pkR ? pkL : pkR;
        } else {
            if (engine->bBand) {
                finalL = engine->band_sample(sampleL, 0);
                finalR = engine->band_sample(sampleR, 1);
            } else {
                if (engine->bClip) {
                    if (sampleL >  1.0) sampleL =  1.0;
                    else if (sampleL < -1.0) sampleL = -1.0;
                    if (sampleR >  1.0) sampleR =  1.0;
                    else if (sampleR < -1.0) sampleR = -1.0;
                }

                if (sampleL >  2.0) sampleL =  2.0;
                else if (sampleL < -2.0) sampleL = -2.0;
                if (sampleR >  2.0) sampleR =  2.0;
                else if (sampleR < -2.0) sampleR = -2.0;

                double wetL, wetR;
                if (engine->bSplit) {
                    wetL = engine->process_sample_multiband(sampleL, 0);
                    wetR = engine->process_sample_multiband(sampleR, 1);
                } else {
                    wetL = engine->process_inflator(sampleL);
                    wetR = engine->process_inflator(sampleR);
                }

                finalL = sampleL + (wetL - sampleL) * engine->fEffect;
                finalR = sampleR + (wetR - sampleR) * engine->fEffect;
            }

            double dL = finalL - sampleL, dR = finalR - sampleR;
            dosePk = fabs(dL) > fabs(dR) ? fabs(dL) : fabs(dR);
        }
        engine->agSumIn  += 0.5 * (rawL * rawL + rawR * rawR);
        engine->agSumFin += 0.5 * (finalL * finalL + finalR * finalR);
        if (dosePk > engine->meterPeak[4]) engine->meterPeak[4] = dosePk;

        // vu-mètres de sortie : après le gain de sortie
        double oL = fabs(finalL * engine->Out_db), oR = fabs(finalR * engine->Out_db);
        if (oL > engine->meterPeak[2]) engine->meterPeak[2] = oL;
        if (oR > engine->meterPeak[3]) engine->meterPeak[3] = oR;

        out_lr[i]   = double_to_int16(finalL * engine->Out_db);
        out_lr[i+1] = double_to_int16(finalR * engine->Out_db);
    }
    engine->update_autogain(frames);
    engine->update_meters(frames);
}

static const mpc_engine_t my_inflator_engine = {
    .create    = mpc_wrapper_create,
    .destroy   = mpc_wrapper_destroy,
    .midi      = mpc_wrapper_midi,
    .set_param = mpc_wrapper_set_param,
    .get_param = mpc_wrapper_get_param,
    .render    = NULL,
    .process   = mpc_wrapper_process,
    .song_pos  = NULL
};

extern "C" {
    const mpc_engine_t *mpc_engine(void) {
        return &my_inflator_engine;
    }

    // Le moteur annonce ici les vu-mètres dont le niveau affiché a changé (une clé par appel, 0 quand il n'y en a plus).
    int mpc_engine_changed(void *inst, char *key, int len) {
        JS_Inflator_Engine* e = static_cast<JS_Inflator_Engine*>(inst);
        static const char *const names[6] = {"in_l", "in_r", "out_l", "out_r", "fx_dose", "in_clip"};
        while (e->meterScan < 6 + NPERSIST) {
            int i = e->meterScan++;
            if (i < 6) {
                if (e->meterStep[i] != e->meterSent[i]) {
                    e->meterSent[i] = e->meterStep[i];
                    snprintf(key, len, "%s", names[i]);
                    return 1;
                }
            } else if (e->persistDirty[i - 6]) {          // réglage restauré : la MPC le relit et met l'écran à jour
                e->persistDirty[i - 6] = 0;
                snprintf(key, len, "%s", PERSIST_KEYS[i - 6]);
                return 1;
            }
        }
        e->meterScan = 0;
        return 0;
    }
}
