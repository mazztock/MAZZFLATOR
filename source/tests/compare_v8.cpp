// MAZZFLATOR - comparaison du nouveau plugin (SDK Akai) avec le moteur MPC V8 d'origine (mpc_wrapper.cpp).
// Copyright (C) 2026 Mazztock - GPL v3 (voir ../LICENSE).
//
// Le nouveau plugin est piloté exactement comme la MPC le fait (interface VST2 : fréquence, taille de bloc,
// « secouage » du paramètre 0, resume, setParameter, processReplacing sur place), et comparé, bloc par bloc,
// au moteur de la v1.0 / « V8 » (reference/mpc_wrapper_v8.cpp, le mpc_wrapper.cpp publié avec la v1.0).
// Mêmes 13 cas et même signal que le test du moteur de la version ordinateur.
//
// Critères : écart audio <= 1 pas 16 bits, vu-mètres / dosage / diode identiques, OUTPUT (AUTOGAIN) identique.
// Lancement : tests/run_compare.sh
#include "mpc_vst2_abi.h"
#include "reference/mpc_wrapper_v8.cpp"   // moteur V8 (int16, 44,1 kHz)

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

extern "C" AEffect* VSTPluginMain(audioMasterCallback host);   // le nouveau plugin (src/MAZZFLATOR.cpp)

static const int SR = 44100, SECONDS = 20, N = SR * SECONDS;
static long gAutomate = 0, gUpdateDisplay = 0;

static VstIntPtr host(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion) return 2400;
    if (opcode == audioMasterAutomate) ++gAutomate;
    if (opcode == audioMasterUpdateDisplay) ++gUpdateDisplay;
    return 0;
}

// signal de test stéréo (identique à test_engine.cpp) : graves + médiums + aigus + bruit,
// enveloppe de -40 dBFS à 0 dBFS avec transitoires à pleine échelle
static std::vector<int16_t> makeSignal()
{
    std::vector<int16_t> s(2 * N);
    unsigned rng = 12345;
    for (int i = 0; i < N; i++) {
        double t = (double)i / SR;
        double env = pow(10.0, (-40.0 + 40.0 * fmod(t, 5.0) / 5.0) / 20.0);
        if (fmod(t, 0.5) < 0.02) env = 1.0;
        rng = rng * 1664525u + 1013904223u;
        double noise = ((rng >> 8) / 16777216.0) * 2.0 - 1.0;
        double l = 0.45 * sin(2 * M_PI * 60 * t) + 0.3 * sin(2 * M_PI * 1000 * t) + 0.15 * sin(2 * M_PI * 6000 * t) + 0.1 * noise;
        double r = 0.45 * sin(2 * M_PI * 80 * t + 1) + 0.3 * sin(2 * M_PI * 1500 * t) + 0.15 * sin(2 * M_PI * 9000 * t) - 0.1 * noise;
        s[2 * i] = double_to_int16(l * env);
        s[2 * i + 1] = double_to_int16(r * env);
    }
    return s;
}

// index des paramètres du nouveau plugin (même ordre que params.json de la V8)
static int paramIndex(const std::string& k)
{
    static const char* keys[] = {"input", "curve", "effect", "output", "clip", "bandsplit", "bypass", "in_l", "in_r",
                                 "out_l", "out_r", "fx_dose", "band", "aa", "ag", "ag_db", "in_clip"};
    for (int i = 0; i < 17; i++) if (k == keys[i]) return i;
    return -1;
}

struct Pair { void* ref; AEffect* fx; };

// Même valeur pour les deux : le plugin reçoit un float (comme de la MPC), la V8 reçoit exactement ce float en texte.
static void both(Pair& p, const char* key, double v)
{
    float f = (float)v;
    char b[64];
    snprintf(b, sizeof b, "%.17g", (double)f);
    mpc_wrapper_set_param(p.ref, key, b);
    p.fx->setParameter(p.fx, paramIndex(key), f);
}

struct Setting { const char* key; double v; };
struct Case { std::string name; std::vector<Setting> set; std::function<void(int, Pair&)> automate; };

int main()
{
    const std::vector<int16_t> sig = makeSignal();
    // automatisation exprimée en temps (blocs de 128 images), quelle que soit la taille de bloc testée
    auto automation = [](int blk128, Pair& p) {
        double t = (double)blk128 / 50.0;
        both(p, "input", 0.5 + 0.4 * sin(t));
        both(p, "curve", 0.5 + 0.5 * sin(0.7 * t));
        both(p, "effect", 0.5 + 0.5 * cos(0.3 * t));
        both(p, "output", 0.8 + 0.2 * sin(0.2 * t));
        if (blk128 % 1500 == 0) both(p, "aa", (blk128 / 1500) % 2);
        if (blk128 % 1100 == 0) both(p, "bandsplit", (blk128 / 1100) % 2);
        if (blk128 % 2300 == 0) both(p, "band", (blk128 / 2300) % 2);
        if (blk128 % 1700 == 0) both(p, "clip", 1 - (blk128 / 1700) % 2);
    };
    const std::vector<Case> cases = {
        {"defaut (EFFECT 0)", {}, nullptr},
        {"EFFECT 100 CURVE 0", {{"effect", 1}}, nullptr},
        {"EFFECT 70 CURVE -50", {{"effect", 0.7}, {"curve", 0}}, nullptr},
        {"EFFECT 100 CURVE +50 INPUT +18", {{"effect", 1}, {"curve", 1}, {"input", 1}}, nullptr},
        {"CLIP off INPUT haut", {{"clip", 0}, {"effect", 1}, {"input", 0.8}}, nullptr},
        {"SPLIT", {{"bandsplit", 1}, {"effect", 1}, {"input", 0.7}}, nullptr},
        {"BAND", {{"band", 1}, {"effect", 1}, {"input", 0.7}}, nullptr},
        {"AA", {{"aa", 1}, {"effect", 1}, {"input", 0.7}}, nullptr},
        {"AA + SPLIT", {{"aa", 1}, {"bandsplit", 1}, {"effect", 0.8}, {"input", 0.6}}, nullptr},
        {"AA + BAND", {{"aa", 1}, {"band", 1}, {"effect", 1}, {"input", 0.7}}, nullptr},
        {"AUTOGAIN", {{"ag", 1}, {"effect", 1}, {"input", 0.8}}, nullptr},
        {"BYPASS", {{"bypass", 1}, {"effect", 1}}, nullptr},
        {"automation (faders et boutons qui bougent)", {{"effect", 0.5}}, automation},
    };

    int fails = 0;
    const int blockSizes[] = {128, 32, 512};
    for (int bs : blockSizes) {
        printf("\n=== blocs de %d images ===\n", bs);
        for (const Case& c : cases) {
            Pair p;
            p.ref = mpc_wrapper_create(nullptr);
            p.fx = VSTPluginMain(host);
            // séquence de la MPC : fréquence, taille de bloc, secouage du paramètre 0, resume
            p.fx->dispatcher(p.fx, effSetSampleRate, 0, 0, nullptr, (float)SR);
            p.fx->dispatcher(p.fx, effSetBlockSize, 0, bs, nullptr, 0.0f);
            float p0 = p.fx->getParameter(p.fx, 0);
            p.fx->setParameter(p.fx, 0, p0 < 0.5f ? 1.0f : 0.0f);
            p.fx->setParameter(p.fx, 0, p0);
            p.fx->dispatcher(p.fx, effMainsChanged, 0, 1, nullptr, 0.0f);
            for (const Setting& s : c.set) both(p, s.key, s.v);

            int maxDiff = 0, meterDiff = 0;
            double outDiff = 0.0;
            std::vector<int16_t> outRef(2 * bs);
            std::vector<float> L(bs), R(bs);
            float* io[2] = {L.data(), R.data()};
            auto* re = static_cast<JS_Inflator_Engine*>(p.ref);
            const int steps[6] = {28, 28, 28, 28, 35, 1};
            const int meterParam[6] = {7, 8, 9, 10, 11, 16};
            for (int pos = 0; pos + bs <= N; pos += bs) {
                if (c.automate && pos % 128 == 0) c.automate(pos / 128, p);
                const int16_t* in = &sig[2 * pos];
                mpc_wrapper_process(p.ref, in, outRef.data(), bs);
                for (int i = 0; i < bs; i++) { L[i] = (float)int16_to_double(in[2 * i]); R[i] = (float)int16_to_double(in[2 * i + 1]); }
                p.fx->processReplacing(p.fx, io, io, bs);   // sur place, comme la MPC
                for (int i = 0; i < bs; i++) {
                    int d1 = abs(double_to_int16((double)L[i]) - outRef[2 * i]);
                    int d2 = abs(double_to_int16((double)R[i]) - outRef[2 * i + 1]);
                    if (d1 > maxDiff) maxDiff = d1;
                    if (d2 > maxDiff) maxDiff = d2;
                }
                for (int m = 0; m < 6; m++) {
                    int got = (int)lround(p.fx->getParameter(p.fx, meterParam[m]) * steps[m]);
                    if (got != re->meterStep[m]) meterDiff++;
                }
                double od = fabs(re->fOutput - (double)p.fx->getParameter(p.fx, 3));
                if (od > outDiff) outDiff = od;
            }
            const bool ok = (maxDiff <= 1 && meterDiff == 0 && outDiff < 1e-6);
            if (!ok) fails++;
            printf("%-45s ecart max %d pas 16 bits | vu-metres differents %d | OUTPUT ecart %.2g  -> %s\n",
                   c.name.c_str(), maxDiff, meterDiff, outDiff, ok ? "OK" : "ECHEC");
            p.fx->dispatcher(p.fx, effMainsChanged, 0, 0, nullptr, 0.0f);
            p.fx->dispatcher(p.fx, effClose, 0, 0, nullptr, 0.0f);
            mpc_wrapper_destroy(p.ref);
        }
    }
    printf("\nappels a la MPC : %ld Automate (vu-metres...), %ld UpdateDisplay (texte GAIN)\n", gAutomate, gUpdateDisplay);
    printf(fails ? "\n%d ECHEC(S)\n" : "\nTOUT EST OK : le nouveau plugin sonne comme la V8\n", fails);
    return fails ? 1 : 0;
}
