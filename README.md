# Delta AB

Plugin VST3 64 bits pour un arbre Delta AB : 3 micros hybrides (capsule omni O + capsule ruban R) disposés en triangle, façon mini arbre Decca.

## Branchement

Une piste à 6 canaux en entrée, une sortie stéréo.

| Entrées | Micro |
| --- | --- |
| 1 et 2 | Micro 1, gauche (O puis R) |
| 3 et 4 | Micro 2, droite (O puis R) |
| 5 et 6 | Micro 3, centre (O puis R) |

Si O et R sont inversés sur un micro, active « Inverser entrées O/R ».

## Chaîne de traitement

Par micro :

1. Routage O / R.
2. Appariement du ruban (±24 dB, au 0,1 dB). « Analyser » mesure O et R sur une octave autour de la coupure : active-le sur un passage de référence, puis désactive-le, et la correction s'inscrit dans « Appariement ».
3. Crossover à 350, 700, 1 500 ou 2 200 Hz, pente 6, 12 ou 24 dB/oct.
4. « Dans le grave » : l'omni ou le ruban.
5. Bloom (capsule dans le grave) et Focus (capsule dans l'aigu), par pas de 0,5 dB.
6. Somme mono, délai (micro central seulement, de −10 à +10 ms au centième), pan G – C – D.

Puis les bus G, C, D : fader, low cut 6 dB/oct (20 à 200 Hz), envoi du centre dans G et D (−1,5 à −12 dB par pas de 1,5 dB), sortie.

## Modes

- **Phase linéaire** : la somme des deux branches redonne exactement le signal, à la précision du 64 bits. « Écoute » utilise un filtre court (environ 65 ms de latence à 48 kHz), « Export » un filtre long (environ 0,7 s). Les deux tournent en temps réel.
- **Phase minimale** : filtres Linkwitz-Riley (1er ordre en 6 dB/oct), latence de 10 ms seulement.

La latence est annoncée au DAW, qui la compense. Les 10 ms fixes servent au délai négatif du micro central (G et D sont alors retardés à sa place).

## Compiler

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DeltaAB_VST3 dsp_tests
ctest --test-dir build -C Release
```

À chaque envoi sur GitHub, la compilation macOS, Windows et Linux se lance toute seule. Le VST3 se télécharge dans l'onglet Actions.

Sur Mac, le plugin n'est pas signé : après l'avoir copié dans `~/Library/Audio/Plug-Ins/VST3/`, lance
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Delta AB.vst3"`.

## Licence de JUCE

JUCE 8 est gratuit pour un usage personnel et en dessous de 50 000 $ de revenus annuels (licence Starter), ou sous AGPLv3.
