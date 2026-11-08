# MAZZFLATOR v1.0 — fiche technique

Version installée sur la MPC le 08/11/2026 (vst.json 2005, uid MzFl). Moteur : `1-projet/mpc_wrapper.cpp`.

## Réglages

| Réglage | Plage | Défaut | Notes |
|---|---|---|---|
| INPUT | -6 → +18 dB | 0 dB (40 % de la course) | deux pentes : -6→0 aussi long que 0→+12 ; repères -6, -3, 0, +6, +12, +18 |
| EFFECT | 0 → 100 % | 0 % | mélange traité / original |
| CURVE | -50 → +50 | 0 | comme le Sonnox et l'Inflator JS (`CURVE_SCALE` 1.0 ; 2.0 donnerait ±100) |
| OUTPUT | 0 → -24 dB | 0 dB | piloté par le moteur quand AUTO GAIN est allumé |
| CLIP (0 dB) | marche / arrêt | marche | écrête l'entrée à 0 dBFS ; arrêté : surcharge utilisable jusqu'à +6 dB |
| SPLIT | marche / arrêt | arrêt | traitement en 3 bandes (240 Hz / 2,4 kHz) |
| BAND | marche / arrêt | arrêt | ajout MAZZFLATOR : seule la bande médium est traitée |
| FX ON | marche / arrêt | marche | bypass (paramètre `bypass` inversé) |
| AUTO GAIN | marche / arrêt | arrêt | ajout MAZZFLATOR : OUTPUT ramène le niveau RMS de sortie à celui d'entrée (borné à -24 dB) |
| AA | marche / arrêt | arrêt | ajout MAZZFLATOR : suréchantillonnage ×2 anti-repliement |

## Courbe de traitement
- En dessous de 0 dBFS : polynôme de l'Inflator JS (A, B, C, D selon CURVE). Il arrive à la pleine échelle à 0 dB avec une pente nulle.
- Au-dessus de 0 dBFS : la sortie reste au plafond (saturation douce). L'Inflator JS repliait le signal (2s − s²) jusqu'à 0 à +6 dB : supprimé dans MAZZFLATOR.
- Entrée bornée à +6 dB (CLIP arrêté) ou 0 dB (CLIP en marche).

## Affichages
- Vu-mètres IN / OUT : 28 segments, crête, retombée 300 ms. Échelle resserrée en haut comme le Sonnox : -42→-10 dB (8 segments), -10→0 (14 segments, ~0,7 dB), 0→+6 (6 segments rouges, surcharge). L'entrée est mesurée après l'écrêtage : avec CLIP elle ne dépasse jamais 0.
- Diode CLIP (rouge, au-dessus des vu-mètres d'entrée) : s'allume à chaque échantillon à 0 dBFS, tenue 2 s (`CLIP_HOLD_S`).
- Dosage (13 LED) et halo orange : crête du signal ajouté par l'effet comparée au niveau moyen d'entrée (~2 s). Échelle -24 → +12 dB, retombée 70 ms (`DOSE_RELEASE_S`). EFFECT à 0 = éteint. Le Sonnox affiche une moyenne : choix volontaire d'un affichage crête, plus vivant.
- Afficheur jaune : valeur d'OUTPUT en dB.

## Skin (layout.conf)
- Coordonnées du layout = pixel du fond + 86 en hauteur. Boutons avec `ns=0` (zone de toucher = image).
- Poignées : 31 images (INPUT, EFFECT, CURVE) et 33 images (OUTPUT), 84 px de large, pour que les repères tombent pile. Le SDK les refait toujours en 36 images.
- Une filmstrip plus large que haute est lue à l'horizontale par le SDK : garder les images plus hautes que larges.
- Mémoire décodée du skin ≈ 19 Mo.
- Le fond se refait avec `4-sources-graphiques/skin2026/generateur/v9.py` (`python3 v9.py 0 bg` puis `node shot.js bg 1`).

## Différences assumées avec le Sonnox Oxford Inflator
Ajouts : AUTO GAIN, AA, BAND, diode tenue 2 s fixe. Absents : fenêtres de valeur chiffrées pour INPUT / EFFECT / CURVE, menu de tenue de la diode (2 s / 5 s / infini), vu-mètres mono/stéréo automatiques, gestionnaire de presets. Dosage en crête au lieu de moyenne.

## Licence
Dérivé de JS Inflator (Kiriki-liszt), sous GPL v3 ; SDK MPC sous licence MIT. Toute version partagée reste sous GPL v3, avec son code source. Ne pas utiliser le nom ni le logo Sonnox. Les deux voies (partage GPL / version fermée) sont détaillées dans FEUILLE_DE_ROUTE_VERSION_ORDI.md.
