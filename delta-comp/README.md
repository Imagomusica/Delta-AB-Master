# Delta Comp

Chaîne de dynamique de mastering en un plugin (VST3 + AU, Mac / Windows), 64 bits, mono ou stéréo.
Esprit D02 / MDC / Solera : on travaille à l'oreille, le sable montre ce qui se passe.

## Chaîne

```
entrée ─ TRANSIT. ─┬─ UPWARD BAS NIV. ─ DOWNWARD BAS NIV. ─ DOUBLE SEUIL (RMS/OPTO) ─ PEAK ─┐
                   └─ DENSITY (parallèle très compressé, remonte le bas niveau) ──────────────┴─ gain auto ─ mix ─ sortie ─ M/S ─ LIMITEUR
           AMBIANCE (remontée des queues, sur le side) après l'étage choisi
```

| Module | Ce qu'il fait |
|---|---|
| Transit. | arrondit (−) ou accentue (+) les attaques, esprit Bittersweet ; Durée = longueur de l'attaque traitée |
| Upward bas niveau | remonte ce qui est sous le seuil (ratio, plage max, attaque, release), jamais le silence (< −60 LUFS) |
| Downward bas niveau | compression douce dès le bas niveau, plafonnée par sa plage |
| Double seuil | seuil 1 (mezzo forte : ratio 1, attaque 1, release 1) et seuil 2 (forte : ratio 2, attaque 2, release 2) ; détection RMS ou OPTO (release qui ralentit après une compression longue et profonde) ; hystérésis : le release ne part que si le gain a plus de X dB à rattraper |
| Peak | compresseur peak, seuil = seuil 1 + Crest (dB) : le crest fixe le rapport entre la compression RMS et la peak |
| Ambiance | extrait le side, le retarde au-delà des premières réflexions, le décorrèle et le remonte par expansion vers le haut sous le seuil : les queues de réverbe montent, pas les transitoires. AUTO RÉFL. estime la fin des premières réflexions après chaque attaque nette et règle le pré-délai. Le mid n'est pas touché (compatible mono) |
| M/S | centre au 0.1 dB, large bande ou filtre (grave, aigu, cloche) à la fréquence choisie ; largeur ; low cut du side 6/12/24 dB/oct ; petite réverbe interne discrète sur Mid, Side ou M+S, envoyée surtout en bas niveau (DYNAMIQUE), pré-délai auto sur les réflexions |
| Limiteur | plafond dBTP, lookahead, release, oversampling OFF/2x/4x/8x, détection true peak, lien des canaux séparé pour les transitoires et le release (0 % = canaux indépendants) |
| Global | cible LUFS (les seuils des étages sont en LU par rapport à elle), Density, lien stéréo, mix parallèle, sortie (avant limiteur), gain auto lent vers la cible |

Les seuils des étages RMS sont en LU relatifs à la cible : si tu changes la cible, toute la chaîne suit.

Latence constante : 5 ms + 16 échantillons (256 à 48 kHz), quels que soient l'oversampling et le lookahead.

## Mesure

Modulomètre Nagra : aiguille noire = peak (montée instantanée, retombe de 20 dB en 1.7 s), aiguille grise = VU (300 ms, calibrée sinus).
Chiffres : peak et true peak L/R en temps réel (maintien 1 s) et max, LUFS M/S/I (BS.1770, I avec portes), RMS 3 s et RMS long, gain auto, fin des réflexions. Clic sur les chiffres = remise à zéro.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDELTACOMP_HOST_TEST=ON
cmake --build build --config Release --target DeltaComp_VST3 DeltaComp_AU dsp_tests host_test
```

Les builds Mac (VST3 + AU universels) et Windows sortent de GitHub Actions (workflow « Build Delta Comp »).
Sur Mac : copier `Delta Comp.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` et `Delta Comp.component` dans `~/Library/Audio/Plug-Ins/Components/`.

Tests : `dsp_tests` (mesures du moteur), `host_test` (charge le VST3 comme un DAW), `ui_snapshot` (capture de l'interface).
