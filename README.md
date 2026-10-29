# Roadie

> Appli guitare tout-en-un : ampli basé sur des captures NAM, looper, caméra synchronisée et export de clip — pour filmer, jouer et exporter sans jongler entre un DAW, OBS et un simulateur d'ampli.

![Roadie — écran principal](docs/screenshot.png)

Nom provisoire. Ce document existe pour que n'importe quelle machine (ou n'importe quelle session Claude) qui récupère ce dépôt ait tout le contexte nécessaire pour continuer, sans dépendre des notes Obsidian du développeur (qui ne suivent pas le dépôt).

---

## 1. État du projet (29/10/2026)

| Jalon | Statut |
|---|---|
| 0 — Préparation | ✅ Clos |
| 1 — Prototype audio | ✅ Clos (carte son, NAM, IR, latence, CPU) |
| 2 — Maquette design | ✅ Clos (direction visuelle définie, mockup HTML interactif) |
| 3 — Looper | ✅ Clos : looper (enregistrer → boucler → overdub, annuler, effacer), métronome calé sur le transport, **format de projet tranché** (§6) |
| 4 — Webcam + synchro | ✅ Clos sur macOS : capture AVFoundation, aperçu live, prises audio + vidéo, calibration de la synchro (§5). Windows/Linux : pas encore de backend caméra |
| 5 — Export clip | ✅ Clos sur macOS : MP4 H.264/AAC (encodeur matériel), vertical 9:16 ou horizontal 16:9, découpe, titre incrusté, audio seul en WAV |
| 6 — Interface finale | ✅ Clos : interface façon DAW du mockup (bibliothèque, pistes, tête de lecture, moniteur caméra flottant, inspecteur, fenêtre d'export), thème sombre et clair, menus macOS |
| 7 — Montage façon GarageBand | ✅ Fait sur la branche `wip/montage-daw` (pas encore dans `main`) : régions éditables (déplacer, rogner, couper, dupliquer, supprimer, ⌘Z / ⇧⌘Z), zone de cycle, zoom, panneaux repliables et redimensionnables, taille de l'interface, CPU en libellé, survols animés. Reste à vérifier à la main avec une vraie prise filmée : vignettes dans les régions, aperçu LECTURE (§9) |

Validé sur macOS 15 (MacBook Pro Intel i5-1038NG7, Command Line Tools seules) : build, tests, interface, webcam FaceTime HD, prise de 3 s audio + vidéo puis export MP4 avec la vraie caméra (avant le montage), gestes d'édition de la timeline au robot souris (`--selftest-edit`). **Pas encore testé avec la Scarlett sur ce Mac** (seulement micro et haut-parleurs intégrés, ~49 ms de latence à cause du micro interne ; ~10 ms attendus avec l'interface, comme sur Linux).

---

## 2. Build

### Dépendances

**macOS** (testé sur macOS 15, Intel) : les **Command Line Tools** suffisent (`xcode-select --install`), Xcode n'est pas nécessaire. Plus CMake :
```bash
brew install cmake
```
Sur un Mac Intel sous macOS 15, Homebrew n'a pas de CMake précompilé et le compile depuis les sources (~20 min) : c'est normal, ce n'est pas bloqué.

**Linux** (testé sur Ubuntu 24.04, avant le passage sur Mac) :
```bash
sudo apt-get install -y cmake libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev \
  libfreetype6-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libfontconfig-dev libxi-dev
```
La caméra et l'export vidéo y sont désactivés (`Source/Platform_stub.cpp`) ; le reste fonctionne.

**Windows** : Visual Studio 2022 + CMake. Pas encore testé ; même situation que Linux pour la caméra.

### Compiler

```bash
git submodule update --init --recursive   # JUCE + NeuralAmpModelerCore (et leurs sous-modules)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Roadie -j
open build/Roadie_artefacts/Release/Roadie.app
```

⚠️ **Toujours compiler en `Release`**. En Debug, le moteur NAM (calcul matriciel Eigen) tourne sans optimisation et sature le CPU — première cause de craquements rencontrée.

⚠️ Si `git submodule update` est interrompu, `libs/NeuralAmpModelerCore` peut rester sur le bon commit mais **sans aucun fichier extrait** (vécu au premier clone sur Mac). Réparer avec :
```bash
git -C libs/NeuralAmpModelerCore checkout -f --detach HEAD && git submodule update --init --recursive
```

**Signature (macOS)** : le build signe l'app en ad hoc (étape post-build). Sans signature, macOS refuse la caméra (et peut refuser le micro) **sans même afficher la demande**. Chaque build ad hoc est une nouvelle identité pour macOS, qui peut redemander les autorisations après une recompilation ; pour l'éviter, signer avec un certificat Apple Development :
```bash
cmake -B build -DROADIE_CODESIGN_IDENTITY="Apple Development: …"
```

### Tests et outils de vérification

```bash
cmake --build build --target RoadieLooperTests RoadieDspTests RoadieEngineTests -j
ctest --test-dir build --output-on-failure
```

| Cible | Ce qu'elle vérifie | JUCE ? |
|---|---|---|
| `RoadieLooperTests` | Looper : record / play / overdub / undo / restart / clear, chargement d'une boucle sauvegardée | non (quelques secondes) |
| `RoadieDspTests` | Égaliseur (courbes), métronome (clics sur les temps), détection d'attaque et mesure de décalage de la synchro, libellé CPU (moyenne glissante + hystérésis) | non |
| `RoadieEngineTests` | Toute la chaîne temps réel via un faux périphérique audio : boucle, mute/solo, pause, overdub, métronome jamais enregistré, prise WAV + horodatage, capture de calibration, une vraie capture NAM, aller-retour d'un projet, **export MP4** 9:16 / 16:9 / plage de la timeline avec titre incrusté (à partir d'une vidéo synthétique) | oui |

Outils :
- `build/RoadieNamBench <capture.nam> <buffer> <sampleRate>` — charge CPU d'une capture (mesures sur ce Mac en §8).
- `Roadie.app --snapshot=/tmp/ui.png [--demo] [--track=amp|loop|camera] [--export] [--light|--dark] [--camera] [--scale=0.9] [--no-library] [--no-inspector]` — rend la fenêtre dans un PNG puis quitte : vérifier l'interface sans capture d'écran (le terminal n'a pas le droit d'enregistrer l'écran). `--demo` ouvre un projet de démo (deux prises synthétiques, une boucle, un cycle) pour voir des régions sans rien filmer. Lancer via `open -W Roadie.app --args …` pour que les autorisations caméra/micro soient demandées au nom de Roadie.
- `Roadie.app --selftest-edit` — ouvre le projet de démo et le monte comme un utilisateur, avec de **vrais événements souris** injectés dans la fenêtre (toute la chaîne JUCE : zones de prise, seuils de glissement, ⌥, double-clic) : déplacer, rogner les deux bords, dupliquer à ⌥, déplacer la boucle, dessiner un cycle, placer la tête dans la règle, ⌘Z ×4 puis ⇧⌘Z, les trois séparateurs, Y et B. Une ligne par vérification sur stderr ; code de sortie = nombre d'échecs. La fenêtre passe au premier plan (info-bulles coupées) : **ne pas toucher la souris pendant les ~5 s du test**. Le robot partage la souris du système : un vrai mouvement au-dessus de la fenêtre, ou une fenêtre qui passe devant (demande d'autorisation, notification), fait échouer des gestes — relancer dans ce cas.
- `Roadie.app --selftest-take=/tmp/dossier` — ouvre la caméra, enregistre une prise de 3 s (audio + vidéo) dans un projet créé dans ce dossier, l'exporte en MP4 et affiche le résultat sur stderr. **Filme réellement** : supprimer le dossier ensuite.

Ces outils sauvegardent `Roadie.settings` au démarrage et le remettent en place en quittant : thème, tailles des panneaux, dernier projet ouvert… restent ceux de l'utilisateur.

---

## 3. Architecture

```
Guitare ─► AmpEngine (gain → NAM → égaliseur → IR → volume) ─┬─► direct ──────────────┬─► mix ─► Metronome ─► sortie
                                                             ├─► TakeRecorder (WAV)   │
                                                             └─► Looper ─► boucle ────┤
                         régions des prises (lues à la position du transport) ────────┘
Caméra ─► CameraCapture (AVFoundation) ─► aperçu + prise .mov (H.264), horodatés sur la même horloge que l'audio
```

**Moteur (sans UI)**
- **`AudioEngine`** — enregistré directement comme `AudioIODeviceCallback` (et non via `AudioAppComponent`) pour recevoir l'horodatage hôte de CoreAudio. Orchestre la chaîne ci-dessus et un **transport linéaire** (lecture, stop, placement de la tête, zone de cycle désactivée pendant une prise), avec des blocs découpés aux frontières du cycle et de la région de boucle. Les régions passent au thread audio sans verrou bloquant (en attente → active → retirée) ; la région du dessus l'emporte quand elles se chevauchent. Mute et volume par piste, vumètres, historique de crêtes (pour dessiner ce qui s'enregistre), capture de calibration. Le callback ne bloque jamais : chargement d'une boucle de projet = `SpinLock` côté message, `TryLock` côté audio (un bloc de silence au pire).
- **`AmpEngine`** — capture `.nam` (NeuralAmpModelerCore), normalisation de niveau d'après la loudness de la capture (−18 dB, comme le plugin NAM : changer de capture ne fait pas sauter le volume), `ToneStack`, IR (`juce::dsp::Convolution`), gain/volume. Swap de modèle thread-safe.
- **`ToneStack`** — Grave / Médium / Aigu / Présence (4 biquads RBJ), sans dépendance JUCE.
- **`Looper`** — mono, temps-réel-safe, sans dépendance JUCE. Sortie de boucle séparée du direct (pour mute/solo), compteur de couches, chargement depuis un projet ; la boucle suit la tête de lecture (`setPlayhead`), calée sur son ancre dans la timeline.
- **`Metronome`** — calé sur la position du transport (le 1er temps de la boucle tombe sur le 1er temps de la mesure), premier temps accentué, mixé au monitoring uniquement.
- **`TakeRecorder`** — écrit la guitare amplifiée en WAV via un FIFO sans verrou (`ThreadedWriter`), note l'heure hôte du premier échantillon et la position de la prise sur la timeline.
- **`SyncAnalysis`** — détection d'attaque et calcul du décalage entre deux captures horodatées, sans dépendance JUCE.
- **`CameraCapture`** (`_mac.mm`) — AVFoundation direct : liste des caméras (intégrée, USB, iPhone via Continuity Camera), autorisations, aperçu réduit, enregistrement H.264 via `AVAssetWriter`, vignettes, micro de la caméra pour la calibration.
- **`ClipExporter`** (`_mac.mm`) — composition AVFoundation : le mixage de la plage exportée + la vidéo de chaque région (segments glissés du décalage de synchro, trous en noir), recadrage 9:16 / 16:9, miroir, titre (rendu CoreText), export `AVAssetExportSession` (H.264/AAC, encodeur matériel).
- **`VideoPreview`** (`_mac.mm`) — images de la vidéo sous la tête de lecture (`AVAssetImageGenerator`) pour le moniteur en mode LECTURE ; les requêtes qui s'empilent sont regroupées, les images en retard abandonnées.
- **`CpuLoad`** — la charge CPU audio en mot (Faible / Normal / Élevé) : moyenne glissante sur ~2 s, seuils à 35 % et 70 % avec une bande morte, sans dépendance JUCE.
- **`Project`** — format de projet (§6). **`CaptureLibrary`** — scan des dossiers de captures en arrière-plan, métadonnées des `.nam`.
- **`Session`** — le contrôleur non visuel : périphérique audio (48 kHz / 64 échantillons demandés), projet, prises, transport en secondes, **arrangement** (toute édition passe par `applyEdit` : 200 niveaux d'annulation, protégés quand une nouvelle boucle a été enregistrée entre-temps), sélection, couper / supprimer / dupliquer / insérer une prise, cache audio des prises (crêtes à 100 par seconde), mixage hors ligne (`renderMix`), calibration, export d'une plage, réglages persistants (`~/Library/Application Support/Roadie/Roadie.settings`). L'UI ne parle qu'à elle.

**Interface** (`Source/UI/`, d'après le mockup, organisée comme GarageBand) — `TopBar` (boutons Bibliothèque / Inspecteur, transport avec looper et cycle, écran mesure/tempo/signature, métronome, vumètre, CPU, latence, Exporter ; version compacte sous ~1120 px), `LibraryPanel` (bibliothèque + recherche), `Timeline` (en-têtes de piste avec fader et vumètre, règle avec zone de cycle, régions Guitare / Looper / Caméra, zoom et défilement, tête de lecture, moniteur caméra DIRECT / LECTURE), `Inspector` (panneau de la piste sélectionnée, en bas), `ExportSheet` (plage : tout le morceau, cycle ou sélection), `Widgets` (dont `HoverFade`, le fondu de survol partagé, et `Splitter`, les séparateurs) et `Icons` (icônes SVG du mockup). `MainComponent` assemble le tout, gère raccourcis, menus macOS (Fichier, Édition, Transport, Audio, Affichage), panneaux repliables, séparateurs, taille de l'interface et glisser-déposer.

### Sous-modules

- `libs/JUCE` — épinglé sur `9.0.4`. ⚠️ JUCE est sous double licence AGPLv3 / commerciale : une distribution non-AGPL de Roadie nécessitera une licence JUCE commerciale.
- `libs/NeuralAmpModelerCore` — épinglé sur `v0.6.0`. Compilé en bibliothèque **OBJECT** `RoadieNAM` partagée par l'app et les tests (voir §8). Dépendances imbriquées : Eigen, `nlohmann/json`, `AudioDSPTools` (présent mais **pas utilisé** — `juce::dsp::Convolution` pour l'IR).

---

## 4. Utilisation

1. **Ampli** : choisir une capture dans la bibliothèque à gauche (dossiers scannés : `~/Music/Roadie/Captures` et `~/Music`, `+` pour en ajouter ; on peut aussi glisser-déposer un `.nam` sur la fenêtre). Régler Gain, Grave, Médium, Aigu, Présence, Volume ; charger une IR de baffle (`.wav`, ou glisser-déposer).
2. **Looper** : `R` ou ⟲ démarre l'enregistrement, `R` ferme la boucle, `R` encore = overdub. La règle affiche la longueur de la boucle en mesures.
3. **Caméra** : piste Caméra → choisir la caméra → Activer. Le moniteur flottant se déplace par sa barre, double-clic pour l'agrandir.
4. **Calibrer** (une fois par combinaison carte son + caméra) : Calibrer, puis un accord étouffé bien sec (ou un clap près d'un micro de la carte son).
5. **Prise** : ● ou `T` enregistre la guitare amplifiée (sans le clic ni la boucle) + la vidéo, à partir de la tête de lecture ; `T` pour arrêter. La prise devient une région de la piste Guitare, liée à sa vidéo sur la piste Caméra, et elle est enregistrée immédiatement dans le projet.
6. **Monter** : glisser une région pour la déplacer (calée sur la grille, ⌘ pour libre, ⌥ pour dupliquer), tirer ses bords pour la rogner, `⌘T` pour la couper à la tête de lecture, `⌫` pour la supprimer, clic droit pour le reste (cycle sur la région, insérer une prise, boucle jusqu'à la fin…). Glisser dans le haut de la règle dessine une zone de cycle (bande jaune) que la lecture répète. `⌘Z` / `⇧⌘Z` annulent et rétablissent (un geste = un ⌘Z).
7. **Exporter** (`⌘E`) : choisir la plage (tout le morceau, zone de cycle ou région sélectionnée), le format, le titre → MP4 dans `exports/` du projet (le mixage de la plage + la vidéo de ses régions) ; ou audio seul en WAV.

L'interface s'adapte à l'écran : `Y` replie la bibliothèque, `B` l'inspecteur ; les séparateurs (bibliothèque | pistes, en-têtes | régions, pistes / inspecteur) se tirent à la souris, double-clic pour la taille par défaut ; Affichage › Taille de l'interface va de 80 à 125 % (90 % par défaut sur un écran de 1440 points ou moins, comme un 13"). Tout est mémorisé dans les réglages.

---

## 5. Synchro audio / vidéo

- La guitare passe par la carte son, l'image par la webcam : deux horloges, deux latences. Roadie horodate chaque bloc audio avec l'horodatage hôte de CoreAudio (`hostTimeNs`, `mach_absolute_time`) et chaque image avec son PTS AVFoundation converti sur la même horloge hôte (`CMSyncConvertTime` depuis l'horloge de session). Le placement de la vidéo dans l'export découle directement de ces deux horodatages (`Take::getVideoOffsetSeconds`).
- Ce que l'estimation ne voit pas (latence réelle des convertisseurs, marge de sécurité CoreAudio) est **mesuré** par la calibration : Roadie écoute 5 s simultanément l'entrée de la carte son et le micro de la caméra (horodaté par AVFoundation comme les images) ; la même attaque entendue des deux côtés donne le décalage (`SyncAnalysis::measureLag`, précision < 1 ms en test). Il est mémorisé et appliqué à chaque prise.
- Un clap n'est pas capté par une entrée guitare (micro de guitare) : d'où la consigne « accord étouffé bien sec », que la carte son reçoit par la guitare et que le micro de la caméra entend acoustiquement.

---

## 6. Format de projet (tranché)

Un projet est un **dossier** `<Nom>.roadie/` (dans `~/Music/Roadie/Projets` par défaut) :

```
Riff du soir.roadie/
├── project.json   tempo, signature, métronome, ampli (capture, IR, potards), caméra, prises, arrangement
├── loop.wav       la boucle, WAV 24 bits mono
├── takes/         take-N.wav (la guitare amplifiée), take-N.mov (la caméra), take-N/ (vignettes)
└── exports/       les clips rendus
```

**Version 2** (montage) : `project.json` porte un objet `arrangement` — les régions (`clips` : prise *par son nom*, `start` sur la timeline, `sourceStart` dans la prise, `length`, en secondes), la place de la boucle (`loopAnchor`, `loopStart`, `loopEnd`, < 0 = jusqu'à la fin), la zone de cycle (`cycleOn`, `cycleStart`, `cycleEnd`) et le volume des pistes en dB. Un projet en version 1 est migré à l'ouverture : ses prises sont posées bout à bout, chacune calée sur une mesure.

Pourquoi un dossier : les médias lourds restent hors du JSON, tout reste lisible avec le Finder, et les prises s'écrivent directement à leur place pendant l'enregistrement (rien à copier au moment de sauvegarder). Chemins internes relatifs ; capture et IR en absolu (elles vivent dans la bibliothèque de l'utilisateur). Le projet est sauvegardé automatiquement après chaque prise et à la fermeture ; `⌘S`, `⇧⌘S` (enregistrer sous, copie le dossier), `⌘O`, `⌘N` ; un dossier `.roadie` se rouvre aussi par glisser-déposer.

---

## 7. Contrôles

| Action | Raccourci |
|---|---|
| Lecture / pause | Espace |
| Retour au début | Entrée |
| Tout arrêter | Échap |
| Looper : enregistrer → boucler → overdub | R |
| Overdub | O |
| Annuler le dernier overdub | U |
| Effacer la boucle | Maj + Retour arrière |
| Enregistrer / arrêter une prise (audio + caméra) | T |
| Métronome | M |
| Zone de cycle | C |
| Mesure précédente / suivante | ← / → |
| Couper la région à la tête de lecture | ⌘T |
| Dupliquer / supprimer la région sélectionnée | ⌘D / ⌫ |
| Annuler / rétablir une édition | ⌘Z / ⇧⌘Z |
| Zoom arrière / avant | ⌘← / ⌘→ (ou pincer, ou ⌘ + molette) |
| Bibliothèque / Inspecteur | Y / B |
| Panneau Ampli / Looper / Caméra | 1 / 2 / 3 |
| Exporter | ⌘E |
| Nouveau / Ouvrir / Enregistrer / Enregistrer sous | ⌘N / ⌘O / ⌘S / ⇧⌘S |
| Réglages audio | ⌘, |
| Thème clair / sombre | ⇧⌘L (menu Affichage) |
| Tempo | glisser sur l'écran Tempo (Maj = fin), double-clic pour saisir |
| Réinitialiser un potard, un fader de piste (0 dB), un séparateur | Double-clic |
| Taille de l'interface (80–125 %) | menu Affichage |

À la souris, dans la timeline : glisser une région (⌘ libre, ⌥ duplique), tirer ses bords (curseur ↔) pour la rogner, clic droit pour le menu ; cliquer ou glisser dans la règle place la tête de lecture (calée sur le temps, ⌘ libre) ; glisser dans sa bande du haut dessine, déplace ou étire la zone de cycle, un clic l'active ou la désactive.

---

## 8. Décisions techniques notables

| Décision | Raison |
|---|---|
| Pas de LTO | ~35 min de lien pour un gain marginal (templates Eigen/NAM) ; `-O3` seul suffit |
| `NAM_SAMPLE_FLOAT` | Les buffers JUCE sont en `float` ; sinon NAM travaille en `double` |
| 48 kHz / 64 échantillons demandés au démarrage | Les captures NAM sont entraînées à 48 kHz ; le défaut système (~81 ms sous PipeWire) était la vraie source de latence. Réglable dans Réglages audio |
| NAM en bibliothèque **OBJECT**, pas STATIC | Chaque architecture (WaveNet, LSTM, Slimmable…) s'enregistre via un initialiseur statique que rien ne référence : dans une archive statique, l'éditeur de liens les élimine → « No config parser registered for architecture » (bug attrapé par `RoadieEngineTests`) |
| Découpage défensif dans `AmpEngine::process` / `AudioEngine` | Certains backends (PipeWire en récupération d'xrun) agrandissent les blocs sans `prepareToPlay` ; NAM/Convolution écriraient hors limites (asserts désactivés en Release) |
| Caméra en AVFoundation direct plutôt que `juce::CameraDevice` | JUCE n'enregistre que la vidéo, horodate la prise à l'heure de la *demande* et non de la première image, fait l'aperçu par une `NSView` native (impossible de dessiner par-dessus) et n'a pas de backend Linux |
| Export via AVFoundation plutôt que FFmpeg | Encodeur matériel, rien à embarquer ni à compiler (pas de bottle FFmpeg pour Intel/macOS 15), recadrage et titre dans la même composition |
| Titre rendu en image CoreText, pas `CATextLayer` | `CATextLayer` sort vide via `AVVideoCompositionCoreAnimationTool` sur macOS |
| Interface « une page façon DAW » (mockup) plutôt que la navigation par pages envisagée au départ | Le mockup retenu montre tout à la fois (pistes + inspecteur contextuel) : sélectionner une piste change l'inspecteur et la couleur d'accent |
| Avenir Next (police système macOS) au lieu de Plus Jakarta Sans / Poppins | Même famille (grotesque géométrique chaleureuse), rien à embarquer ni de licence à vérifier ; chiffres tabulaires simulés (`roadie::fonts::drawTabular`) |
| Majuscules des libellés faites à la main (`toUpperFrench`) | `juce::String::toUpperCase()` transforme « è » en « Î » sur macOS |
| `Looper`, `ToneStack`, `Metronome`, `SyncAnalysis` sans JUCE | Testables en quelques secondes avec un simple compilateur |
| Signature ad hoc post-build | macOS refuse la caméra aux apps non signées sans afficher de demande (les binaires Intel ne sont pas signés par l'éditeur de liens) |
| Taille de l'interface par `Desktop::setGlobalScaleFactor` | Tout est dessiné à la main en coordonnées logiques : un seul facteur met tout à l'échelle (menus et info-bulles compris), la fenêtre garde sa taille à l'écran et montre plus de timeline quand on réduit |
| Survols : un `HoverFade` par cible, piloté par `paint`, un seul minuteur partagé | Fondu de ~120 ms sans un `Timer` par contrôle ; le minuteur ne tourne que tant qu'un fondu bouge |
| CPU en mot (Faible / Normal / Élevé) plutôt qu'en pourcentage | Le pourcentage instantané clignotait ; moyenne sur ~2 s et bande morte de ±3 % autour des seuils, le pourcentage reste dans l'info-bulle |
| Autotest des gestes par `ComponentPeer::handleMouseEvent` | Les événements traversent toute la chaîne JUCE comme ceux du système. Deux pièges : le pair travaille en points natifs (multiplier par l'échelle de l'interface) et JUCE lit ⌥ / ⌘ dans `ModifierKeys::currentModifiers`, pas dans l'événement |

Mesures NAM sur ce Mac (i5-1038NG7, `RoadieNamBench`, 48 kHz) : captures Fender « Slimmable » ~17 % d'un cœur à 64 échantillons (19 % à 32, 16 % à 256) ; WaveNet standard ~21 % à 64. Nettement plus que sur le PC Linux (6-8 %), mais le buffer de 64 reste confortable.

---

## 9. Pour la suite

1. **Tester avec la Scarlett sur le Mac** : latence réelle, calibration de la synchro avec un accord étouffé, prises longues — et, avec une vraie prise filmée, les vignettes dans les régions Caméra et l'aperçu LECTURE du moniteur (seuls points du montage que les outils ne vérifient pas, avec le menu contextuel). Puis fusionner `wip/montage-daw` dans `main`.
2. **Caméra hors macOS** : backend Media Foundation (Windows) et V4L2/GStreamer (Linux) derrière `CameraCapture` ; export équivalent (Media Foundation / FFmpeg).
3. **Polices de la DA** : embarquer Plus Jakarta Sans + Poppins (OFL) via `juce_add_binary_data` si on veut l'identité exacte du mockup hors macOS.
4. **Distribution** : signature Developer ID + notarisation, et licence JUCE (AGPL ou commerciale).
5. **Idées** : plusieurs boucles / pistes de looper, quantification de la longueur de boucle à la mesure, décompte avant enregistrement, choix d'une partie de la boucle dans l'export.
