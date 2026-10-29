# Chantier : montage façon GarageBand

Branche : `wip/montage-daw`. **Tout le plan ci-dessous est fait** (29/10,
sur Mac) : build sans avertissement dans le code Roadie, CTest, captures
`--snapshot` en clair, en sombre et à plusieurs tailles d'interface, et les
gestes d'édition vérifiés par `--selftest-edit` (vrais événements souris).
`main` reste sur 1cf9387 jusqu'à l'essai réel ci-dessous, puis la branche
pourra y être fusionnée.

**Reste, à faire à la main avec une vraie prise filmée** : les vignettes
dans les régions Caméra, l'aperçu LECTURE du moniteur, le menu contextuel
des régions, et l'impression générale des survols.

## Demande d'origine

1. CPU : afficher Faible / Normal / Élevé au lieu d'un pourcentage qui clignote.
2. Des survols (hover) sur tous les contrôles, discrets mais vivants.
3. Une interface « dézoomée » pour un écran 13", et des séparateurs entre les panneaux qu'on déplace à la souris (curseur double flèche).
4. Des outils de montage : rogner, couper, déplacer, supprimer, annuler.
5. Une organisation et une lisibilité proches de GarageBand, en gardant la vidéo.

## Analyse GarageBand → décisions

- **Barre du haut** : boutons Bibliothèque et Inspecteur à gauche, transport au centre (début, lecture, stop, enregistrement, cycle), LCD mesure·temps / tempo / signature, métronome. À droite : vumètre d'entrée, CPU, latence, réglages, Exporter.
- **Pistes** : en-têtes à gauche (icône carrée de couleur, nom, M/S, curseur de volume, petit vumètre). Lignes de hauteur égale, avec des régions colorées par piste qui portent leur nom en haut à gauche et leur forme d'onde.
- **Règle** : numéros de mesure. Une bande « cycle » jaune (`exportAccent`) dans le haut de la règle. Un clic dans la règle déplace la tête de lecture, qui se termine par un triangle.
- **Inspecteur en bas** (Smart Controls), repliable avec B ; Bibliothèque repliable avec Y.
- **Zoom horizontal** : curseur en haut à droite et ⌘← / ⌘→ ; défilement horizontal avec le trackpad ; la tête de lecture est suivie pendant la lecture.
- **Pistes retenues** : *Guitare* (ampli + régions des prises), *Looper* (région de boucle), *Caméra* (vidéo des régions de la piste Guitare, liée à leur audio comme un clip iMovie). L'ancienne piste « Guitare DI » est fusionnée dans Guitare.

## Fait (non compilé de bout en bout)

- `Project.h/.cpp` : ajout de `Clip` {take, start, sourceStart, length} et d'`Arrangement` {clips, loopAnchor, loopStart, loopEnd (<0 = jusqu'à la fin)}. Le projet stocke aussi cycleOn/Start/End et le volume de chaque piste en dB.
  - Le format passe en version 2 ; les clips référencent les prises par leur nom.
  - Un projet en v1 est migré en posant les prises bout à bout, chacune calée sur une mesure.
- `Looper` : ajout de `update()` (applique les demandes sans produire de son) et de `setPlayhead()` (la boucle suit la timeline). `setContent` laisse désormais la boucle en état Playing. Les tests ont été adaptés.
- `AudioEngine`, entièrement réécrit :
  - **transport** linéaire (play, stop, locate) et zone de cycle, désactivée pendant une prise ;
  - **découpage des blocs** aux frontières du cycle et de la région de boucle ;
  - **régions** passées au thread audio sans verrou bloquant (pending → active → retired), la région du dessus l'emporte quand elles se chevauchent ;
  - **boucle** calée sur son ancre : son premier échantillon fixe l'ancre, et `loopRecordCount` signale chaque nouvelle boucle ;
  - **mixage** : mute et volume par piste, vumètres guitare et boucle, historique de crêtes toutes les 10 ms (pour dessiner la région en cours d'enregistrement) ;
  - **prises** : elles enregistrent uniquement la guitare amplifiée, avec sa position sur la timeline (`getTakeStartPosition`).
- `Session` :
  - API du transport en secondes : locate, jumpBars, cycle ;
  - pistes `Track { amp, loop, camera }` (l'ancienne `guitar` est supprimée) ;
  - `applyEdit`, plus annuler/rétablir avec 200 niveaux et protection quand une nouvelle boucle a été enregistrée entre-temps ;
  - sélection d'une région ou de la boucle ;
  - `splitAtPlayhead`, `deleteSelection`, `duplicateSelection`, `insertTake`, `setLoopOpenEnded` ;
  - cache audio des prises (crêtes à 100 par seconde, rééchantillonnage si la fréquence du périphérique change) ;
  - prévisualisation vidéo de la région sous la tête de lecture ;
  - `renderMix` (mixage hors ligne), `getVideoSegments`, puis `exportClip` / `exportAudio` sur une plage `[startSeconds, endSeconds]`.
- `VideoPreview.h` + `VideoPreview_mac.mm` : décodage d'images avec AVAssetImageGenerator ; les requêtes qui s'empilent sont regroupées et les images en retard sont abandonnées. Le stub non-Apple est dans `Platform_stub.cpp`.
- `ClipExporter` : prend un audio (le mixage) et une liste de `VideoSegment` ; les trous sont noirs.
- `CMakeLists.txt` : ajout de `VideoPreview_mac.mm`.

Vérifié : `Session`, `AudioEngine`, `Project`, `Looper`, `VideoPreview` et
`ClipExporter` compilent sans erreur. Il reste des avertissements à nettoyer :
`-Wfloat-equal` dans les `operator==` de `Project.h` (à faire avec
`juce::exactlyEqual`) et le `?:` GNU dans `ClipExporter_mac.mm:64`.

## Reste à faire (dans l'ordre)

1. ~~**Faire compiler**~~ **Fait (26/10, sur Linux)** :
   - `Main.cpp` : le selftest construit l'`ExportRequest` à partir du clip
     que `finishTake()` vient de sélectionner (`start`/`end()`), plus de
     `takeIndex`.
   - `Inspector.cpp` : la traduction `Track::guitar → Track::amp` était un
     reliquat d'avant la fusion des pistes ; supprimée (plus personne
     n'envoie `Track::guitar`, qui n'existe plus dans l'enum).
   - `UI/Timeline.cpp` : la piste DI séparée (`Track::guitar`, 4e ligne)
     n'existe plus — une seule piste « Guitare » (= `Track::amp`) affiche
     maintenant le sweep de l'ampli. `getTimelineLength()`/`getInputSlot()`/
     `getOutputSlot()`/`numSlots` n'existent pas sur `AudioEngine` (qui ne
     connaît que samples/clips/boucle, pas la notion de projet) ; remplacés
     par un helper `timelineWindowSamples(session)` (= `Session::getContentEnd()`
     convertie en échantillons) et par le seul historique de crêtes réel que
     le moteur expose (`getHistoryPeak`/`getHistorySlotSamples`/
     `historyCapacity`, celui de l'ampli — il n'y a pas d'historique séparé
     pour l'entrée brute). La sélection de prise dans la piste Caméra passe
     maintenant par le clip sélectionné (`getSelectedClip()` → `.take`).
   - `UI/ExportSheet.cpp/h` : réécrit pour choisir une **plage** (tout le
     morceau / zone de cycle / sélection) au lieu d'une prise dans une liste
     déroulante — exactement ce que demandait la section "Demande d'origine".
     `exportTakeAudio` → `exportAudio`, `trimStartSeconds/EndSeconds` →
     `startSeconds/endSeconds` directement.
   - `Tests/EngineTests.cpp` : `setLiveMuted` → `setGuitarMuted`. Un vrai bug
     fonctionnel trouvé en lançant `ctest` (pas juste un renommage) : le
     test du métronome ne démarrait jamais le transport, et le métronome est
     maintenant calé sur la position du transport (il ne clique plus en
     roue libre) — corrigé en ajoutant `rig.engine.play()`.
   - **`ctest --test-dir build`** : looper/dsp/engine passent tous les
     trois, y compris un vrai chargement NAM (`wavenet.nam`, 1,2 % CPU pour
     2 s audio). L'app (`Roadie`) tourne, buffer 64 échantillons, ~9-10 ms
     de latence round-trip — cohérent avec les mesures d'avant le montage.
   - Pas touché aux fichiers `_mac.mm` (`CameraCapture_mac.mm`,
     `ClipExporter_mac.mm`, `VideoPreview_mac.mm`, `Tests/TestMovie_mac.mm`) :
     impossibles à compiler/tester sur cette machine Linux. `testClipExport`
     (caméra + export MP4 réels) est `#if JUCE_MAC`, jamais exécuté ici.
2. ~~**Réécrire `UI/Timeline`**~~ **Fait (28/10, sur Mac)** :
   - en-têtes de piste : icône, nom, sous-titre, M/S, fader de volume
     (double-clic → 0 dB), mini-vumètre, survol et sélection ;
   - règle : clic/glisser pour placer la tête de lecture (calée sur le
     temps, ⌘ pour libre), bande de cycle en haut (glisser pour créer,
     déplacer, redimensionner ; clic pour activer/désactiver), ligne
     fantôme au survol ;
   - régions Guitare avec forme d'onde, région rouge pendant une prise
     (historique de crêtes) et pendant l'enregistrement d'une boucle ;
     région de boucle crantée à chaque répétition ; piste Caméra liée aux
     régions Guitare (vignettes, « audio seul » sinon) ;
   - glisser pour déplacer (grille ; ⌘ libre ; ⌥ duplique), rogner par les
     bords (curseur ↔), un seul ⌘Z par geste ;
   - menu contextuel : couper, dupliquer, supprimer, cycle sur la région,
     exporter la région, insérer une prise, boucler jusqu'à la fin /
     arrêter la boucle à la tête de lecture, effacer la boucle ;
   - zoom (curseur en haut à gauche, ⌘← / ⌘→, pincement, ⌘ + molette),
     défilement horizontal (trackpad, barre), suivi de la tête de lecture ;
   - moniteur caméra « DIRECT » / « LECTURE ».
   - Vérifié : build, CTest, snapshots `--snapshot=… --demo` en clair et en
     sombre (`--demo` crée un projet de démo avec deux prises synthétiques
     et une boucle, sans filmer).
   - **Pas encore vérifié à la main** : les gestes souris (déplacer,
     rogner, dupliquer, menu, cycle dessiné), les vignettes vidéo dans les
     régions et l'aperçu LECTURE avec une vraie prise filmée.
3. ~~**Raccourcis**~~ **Fait (29/10)** : B inspecteur, Y bibliothèque (repliés
   ou non, mémorisé), menu **Édition** (Annuler / Rétablir avec le nom de
   l'action, couper, dupliquer, supprimer, tout désélectionner, zone de
   cycle) ; menu Affichage : Bibliothèque, Inspecteur, zoom avant / arrière,
   tout le morceau à l'écran, Taille de l'interface, thème clair.
4. ~~**Séparateurs déplaçables**~~ **Fait (29/10)** : `ui::Splitter` entre
   bibliothèque et pistes (170–360 px), entre en-têtes et régions (150–300 px)
   et au-dessus de l'inspecteur (100–200 px, les potards grandissent avec) ;
   curseur ↔ / ↕, trait jaune au survol, double-clic = taille par défaut,
   tailles mémorisées (`libraryWidth`, `trackHeaderWidth`, `inspectorHeight`).
5. ~~**Échelle d'interface**~~ **Fait (29/10)** : Affichage > Taille de
   l'interface 80 / 90 / 100 / 110 / 125 % (`uiScale`), appliquée à chaud ;
   sans choix enregistré, 90 % si l'écran fait 1440 pt ou moins. La fenêtre
   s'ouvre à ~92 % de l'écran (en coordonnées mises à l'échelle).
6. ~~**TopBar**~~ **Fait (29/10)** : CPU en mot (`CpuLoad.h`, moyenne ~2 s,
   seuils 35 / 70 % ± 3 %, testé dans `RoadieDspTests`, pourcentage dans
   l'info-bulle) ; boutons Bibliothèque et Inspecteur à gauche, Cycle dans le
   transport, Métronome après l'écran ; barre de 46 px au lieu de 54 ; sous
   ~1120 px de large, plus de nom « Roadie » ni de libellé « Latence ».
7. ~~**Survol**~~ **Fait (29/10)** : `ui::HoverFade` (fondu de 120 ms piloté
   par `paint`, un seul minuteur partagé qui s'arrête quand tout est posé)
   sur les boutons, les cases de l'écran (tempo, signature), les lignes de la
   bibliothèque, les en-têtes de piste, les faders, M / S, le zoom, les
   régions et les séparateurs ; les boutons s'enfoncent légèrement au clic.
8. ~~**Vérification**~~ **Fait (29/10)** : build, CTest, snapshots, et
   `--selftest-edit` : 19 vérifications (déplacer, rogner les deux bords,
   dupliquer à ⌥, déplacer la boucle, dessiner un cycle, tête de lecture dans
   la règle, ⌘Z ×4 puis ⇧⌘Z, les trois séparateurs, Y / B). Les outils de
   vérification remettent `Roadie.settings` comme ils l'ont trouvé.
   Pas fait : l'essai réel à la caméra (voir en haut).
9. ~~**README**~~ **Fait (29/10)** : jalon 7, outils, architecture, format v2,
   utilisation et raccourcis du montage.
