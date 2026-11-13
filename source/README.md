# MAZZFLATOR v2.0 (SDK officiel Akai)

Saturation douce façon « Inflator » pour MPC, par Mazztock. Dérivé de [JS Inflator](https://github.com/Kiriki-liszt/JS_Inflator) (Kiriki-liszt), **GPL v3** (voir `LICENSE`).
Projet indépendant, sans lien avec Sonnox ; « Inflator » est une marque de Sonnox Ltd.
Le framework du SDK Akai (`framework/`) est sous licence MIT, compatible avec la GPL v3.

## Réglages (même ordre et mêmes identifiants que la v1.0)

Le skin désigne les paramètres par numéro (`Parameter N`) : ne jamais réordonner, ajouter à la fin.

| # | id | Rôle |
|---|---|---|
| 0 | input | INPUT -6 → +18 dB (0 dB à 40 %) |
| 1 | curve | CURVE -50 → +50 |
| 2 | effect | EFFECT 0 → 100 % (0 = éteint) |
| 3 | output | OUTPUT 0 → -24 dB (piloté par le moteur en AUTOGAIN) |
| 4 | clip | CLIP 0 dB (allumé par défaut) |
| 5 | bandsplit | SPLIT (3 bandes) |
| 6 | bypass | BYPASS (« FX ON » sur le skin) |
| 7–11 | in_l, in_r, out_l, out_r, fx_dose | affichage : vu-mètres 28 segments, dosage / halo |
| 12 | band | BAND (médiums 240 Hz – 2,4 kHz seuls) |
| 13 | aa | AA, anti-aliasing x2, latence 23 échantillons |
| 14 | ag | AUTOGAIN |
| 15 | ag_db | affichage : texte GAIN |
| 16 | in_clip | affichage : diode CLIP d'entrée (tenue 2 s) |

## Fichiers

- `src/MazzflatorEngine.h/.cpp` : le moteur, partagé avec la version ordinateur (JUCE). Toute correction du son se fait là, une seule fois, et se teste des deux côtés.
- `src/MAZZFLATOR.cpp` : le lien avec la MPC (paramètres, textes affichés, vu-mètres, AUTOGAIN, plafond 0 dBFS).
- `skins/` : le skin 2026 (`TUI.json`, `Q-Links.json`, `Q-Links - 8by1.json`, images), recopié tel quel dans `Plugin Skins/` par le build (pas de `ui.json`).
  Il importe deux fichiers de la MPC (`/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json` et `Generic Menu Overlay.json`) pour la grande fenêtre ouverte d'un double tap : référencés, non copiés.
- `tests/run_compare.sh` : compare ce plugin au moteur de la v1.0 (`tests/reference/mpc_wrapper_v8.cpp`, interface `engine.h` de mpc-vst-plugins, MIT) sur 13 cas et 3 tailles de bloc.

## État

- Testé sur MPC Live, MPC OS 3.10.1 : skin, son, vu-mètres, diode, halo, AUTOGAIN, AA, rappel des réglages.
- Compilation arm32 + arm64 OK, simulateur MPC OK, sortie identique à la v1.0 à 1 pas 16 bits près (arrondi) sur les 13 cas.
- Différences voulues avec la v1.0 : calcul en 32 bits flottants au lieu de 16 bits entiers (un peu moins de bruit d'arrondi) ; sortie toujours plafonnée à 0 dBFS comme la v1.0.
- Les projets enregistrés avec la v1.0 (SDK mpc-vst-plugins) ne retrouvent pas leurs réglages : le format de sauvegarde a changé.
- La latence de l'AA (23 échantillons) est annoncée sans rappel à l'hôte.
- Non testé sur une MPC 64 bits (Live III, XL) : la version arm64 est compilée et validée, pas essayée sur la machine.

## Compiler et tester

Depuis le dossier du SDK, avec ce projet copié dans `plugins/MAZZFLATOR/` :

```
python3 tools/build.py plugins/MAZZFLATOR
.sdk-tools/host_sim plugins/MAZZFLATOR/build/native/mazzflator.dylib test
plugins/MAZZFLATOR/tests/run_compare.sh
```
