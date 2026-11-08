# MAZZFLATOR — loudness & saturation plugin for Akai MPC (MPC OS 3.x)

![MAZZFLATOR](apercu.png)

**FR** — MAZZFLATOR est un effet de densité et de saturation pour les MPC autonomes (MPC Live, One, X, Key, Force), inspiré du fonctionnement de l'Oxford Inflator de Sonnox. Il dérive de [JS Inflator](https://github.com/Kiriki-liszt/JS_Inflator) de Kiriki-liszt et tourne grâce au SDK [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins). Gratuit et libre, sous licence GPL v3.

**EN** — MAZZFLATOR is a loudness / density / saturation effect for standalone Akai MPCs, modelled on the behaviour of the Sonnox Oxford Inflator. Derived from Kiriki-liszt's JS Inflator, built with the mpc-vst-plugins SDK. Free software, GPL v3.

## Réglages / Controls
- **INPUT** -6 → +18 dB (0 dB at 40 % of travel) · **EFFECT** 0–100 % · **CURVE** -50 → +50 · **OUTPUT** 0 → -24 dB
- **CLIP 0dB** (on by default), **SPLIT** (3 bands), **BAND** (mid band only), **FX ON**
- **AUTO GAIN** (matches output loudness to input), **AA** (2× oversampling)
- Input / output meters (Sonnox-style scale, red overdrive zone up to +6 dB), input **CLIP** light (2 s hold), live effect meter with a glowing frame that follows the dose.

Details: [docs/FICHE_TECHNIQUE.md](docs/FICHE_TECHNIQUE.md) (FR).

## Installation
1. Download `MAZZFLATOR-v1.0-MPC.zip` from the **Releases** page and unzip it.
2. Copy the folder `Mazztock - VST - MAZZFLATOR` to `/sdcard/Synths/` on the MPC (SSH or the SDK installer), replacing any older version.
3. Restart the MPC.

## Compiler / Build
Needs the [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) SDK and Docker. Copy `source/` to `mpc-vst-plugins/ports/mazzflator/`, then:
```
MPC_VST=~/mpc-vst-plugins/sdk ~/mpc-vst-plugins/sdk/tools/build_port.sh vst.json
```
Engine: `source/mpc_wrapper.cpp` · Layout: `source/layout.conf` · Skin images: `source/images/`.

## Crédits / Credits
- DSP based on **JS Inflator** by Kiriki-liszt (GPL v3), itself a recreation of the Sonnox Oxford Inflator.
- MPC plugin SDK: **mpc-vst-plugins** by sd88me (MIT).
- MAZZFLATOR, skin 2026 and changes (two-slope input, soft ceiling above 0 dBFS, auto gain, AA, BAND, metering): Mazztock, made with Claude (Anthropic).
- Sonnox and Oxford Inflator are trademarks of Sonnox Ltd. This project is not affiliated with Sonnox.

## Licence
GPL v3 — see [LICENSE](LICENSE). Copyright © 2026 Mazztock.
