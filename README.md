# Delta AB

Plugin VST3 64 bits, une seule console : le Delta AB (3 micros hybrides en triangle, façon mini arbre Decca), le Delta AER (3 omnis d'ambiance G, C, D) et jusqu'à 16 spots EN-MS (cardio + ruban), puis les bus.

## Branchement

Une piste de 47 canaux en entrée (le DAW les voit comme des bus, à la suite) :

| Entrées | Source |
| --- | --- |
| 1 à 4 | Micro 1, gauche : O1 MK221, O2 MK102, R1 (aigu), R2 (grave) |
| 5 à 8 | Micro 2, droite : idem |
| 9 à 12 | Micro 3, centre : idem |
| 13 à 15 | Delta AER : G, C, D |
| 16 à 47 | Spots EN-MS 1 à 16 : cardio puis ruban |

Une piste de 6 canaux (O, R par micro) marche aussi pour le Delta AB.

Sorties : la sortie stéréo principale, plus deux sorties stéréo à activer dans le DAW si besoin : « AER séparé » et « Spots séparés ». Activées, l'AER ou les spots sortent là au lieu d'être mélangés, pour passer dans des inserts du DAW (réverbe, comp/EQ, ou insert hardware DA/AD).

## Delta AB, par micro

1. Choix de 2 sources sur 4 (X et Y), à l'oreille.
2. Gain match de Y (±24 dB au 0,1 dB). « Analyser » mesure X et Y sur une octave autour de la coupure ; quand on le désactive, la correction s'inscrit.
3. Crossover à 350, 700, 1250, 1600, 1950, 2200 ou 4400 Hz (ou « Focal », le point global), 6, 12 ou 24 dB/oct, phase linéaire ou minimale, sans perte au point de coupure (Linkwitz-Riley).
4. Swap : X ou Y dans le grave. « Ambiance » inverse les 3 micros d'un coup.
5. Aigu (Focus) et grave (Bloom) au 0,1 dB, délai de −10 à +10 ms par 0,5 ms.
6. Somme mono, niveau de sortie, pan total.

Macros : Focus (plus ou moins d'aigu), Bloom (plus ou moins de grave, ou grave plus et aigu moins), Focal (point de crossover), Ambiance (swap).

## Delta AER, par canal

Low cut et high cut (6, 12 ou 24 dB/oct), shelf grave et aigu avec Q, 3 cloches avec Q. Phase minimale (biquads) ou linéaire (FIR qui a exactement la même courbe). Délai de −10 à +100 ms, affiché aussi en mètres (343 m/s). Fader, faders liés en option (bouger un fader déplace les deux autres du même nombre de dB). Pan total. Puis somme G, C, D.

## Spots EN-MS, par tranche

8 tranches de base, « + TRANCHE » en ajoute une (jusqu'à 16 ; les tranches non ajoutées sont coupées et ne coûtent rien).

1. Gain match du ruban.
2. Mode M/S : cardio (M) + ruban (S) en mono ; le dosage des deux oriente le micro virtuel, « ruban côté opposé » le tourne de l'autre côté. Ou mode Crossover, le même que le Delta AB (cardio ou ruban dans le grave).
3. Niveaux cardio et ruban, délai (−10 à +100 ms, en ms et en mètres), fader, pan total.

## Pan total

Le pan se fait entre deux bus voisins (G–C ou C–D), avec des indices qu'on dose séparément, sans coloration :

- **Niveau** : loi de pan −3, −4,5 ou −6 dB au milieu.
- **Délai** : le bus le plus éloigné est retardé, jusqu'à 0,7 à 11,2 ms, ou 30 ms (Haas). Pur retard, amplitude plate.
- **Phase** : décalage de phase constant entre les deux bus, jusqu'à ±45°. Transformée de Hilbert en FIR à phase linéaire : amplitude plate.
- **Pan HF** : le grave (sous 700 Hz) reste plus au centre que l'aigu. Séparation à phase linéaire, somme exacte.

Bus G, C, D du Delta AB : fader, low cut 6 dB/oct (20 à 200 Hz). Le centre est envoyé dans G et D (−1,5 à −12 dB par pas de 1,5 dB), pour le Delta AB, l'AER et les spots. Puis faders AER, Spots et Master.

## Modes

- **Phase linéaire** : la somme des deux branches redonne exactement le signal, à la précision du 64 bits. « Écoute » utilise un filtre court (environ 100 ms de latence à 48 kHz, panpot compris), « Export » un filtre long (environ 1 s). Les deux tournent en temps réel (export avec 16 spots : environ un tiers d’un cœur).
- **Phase minimale** : filtres Linkwitz-Riley (1er ordre en 6 dB/oct), latence d’environ 45 ms (délai du centre et panpot de phase).

La latence est annoncée au DAW, qui la compense. Les 10 ms fixes servent aux délais négatifs (les autres voies sont alors retardées à leur place).

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
