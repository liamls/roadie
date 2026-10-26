# Roadie

> Appli guitare tout-en-un : ampli basé sur des captures NAM, looper, caméra synchronisée et export de clip — pour filmer, jouer et exporter sans jongler entre un DAW, OBS et un simulateur d'ampli.

![Roadie — écran principal](docs/screenshot.png)

Nom provisoire. Ce document existe pour que n'importe quelle machine (ou n'importe quelle session Claude) qui récupère ce dépôt ait tout le contexte nécessaire pour continuer, sans dépendre des notes Obsidian du développeur (qui ne suivent pas le dépôt).

---

## 1. État du projet (26/10/2026)

| Jalon | Statut |
|---|---|
| 0 — Préparation | ✅ Clos |
| 1 — Prototype audio | ✅ Clos (carte son, NAM, IR, latence, CPU) |
| 2 — Maquette design | ✅ Clos (direction visuelle définie, mockup HTML interactif) |
| 3 — Looper | ✅ Clos : looper (enregistrer → boucler → overdub, annuler, effacer), métronome calé sur le transport, **format de projet tranché** (§6) |
| 4 — Webcam + synchro | ✅ Clos sur macOS : capture AVFoundation, aperçu live, prises audio + vidéo, calibration de la synchro (§5). Windows/Linux : pas encore de backend caméra |
| 5 — Export clip | ✅ Clos sur macOS : MP4 H.264/AAC (encodeur matériel), vertical 9:16 ou horizontal 16:9, découpe, titre incrusté, audio seul en WAV |
| 6 — Interface finale | ✅ Clos : interface façon DAW du mockup (bibliothèque, pistes, tête de lecture, moniteur caméra flottant, inspecteur, fenêtre d'export), thème sombre et clair, menus macOS |

Validé sur macOS 15 (MacBook Pro Intel i5-1038NG7, Command Line Tools seules) : build, tests, interface, webcam FaceTime HD, prise de 3 s audio + vidéo puis export MP4 avec la vraie caméra. **Pas encore testé avec la Scarlett sur ce Mac** (seulement micro et haut-parleurs intégrés, ~49 ms de latence à cause du micro interne ; ~10 ms attendus avec l'interface, comme sur Linux).

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
| `RoadieDspTests` | Égaliseur (courbes), métronome (clics sur les temps), détection d'attaque et mesure de décalage de la synchro | non |
| `RoadieEngineTests` | Toute la chaîne temps réel via un faux périphérique audio : boucle, mute/solo, pause, overdub, métronome jamais enregistré, prise WAV + horodatage, capture de calibration, une vraie capture NAM, aller-retour d'un projet, **export MP4** 9:16 / 16:9 / découpé avec titre incrusté (à partir d'une vidéo synthétique) | oui |

Outils :
- `build/RoadieNamBench <capture.nam> <buffer> <sampleRate>` — charge CPU d'une capture (mesures sur ce Mac en §8).
- `Roadie.app --snapshot=/tmp/ui.png [--track=amp|loop|camera] [--export] [--light|--dark] [--camera]` — rend la fenêtre dans un PNG puis quitte : vérifier l'interface sans capture d'écran (le terminal n'a pas le droit d'enregistrer l'écran). Lancer via `open -W Roadie.app --args …` pour que les autorisations caméra/micro soient demandées au nom de Roadie.
- `Roadie.app --selftest-take=/tmp/dossier` — ouvre la caméra, enregistre une prise de 3 s (audio + vidéo) dans un projet créé dans ce dossier, l'exporte en MP4 et affiche le résultat sur stderr. **Filme réellement** : supprimer le dossier ensuite.

---

## 3. Architecture

```
Guitare ─► AmpEngine (gain → NAM → égaliseur → IR → volume) ─► live ─┬─────────────► mix ─► Metronome ─► sortie
                                                                     └─► Looper ─► boucle ┘    │
                                                                                               └─► TakeRecorder (WAV, sans clic)
Caméra ─► CameraCapture (AVFoundation) ─► aperçu + prise .mov (H.264), horodatés sur la même horloge que l'audio
```

**Moteur (sans UI)**
- **`AudioEngine`** — enregistré directement comme `AudioIODeviceCallback` (et non via `AudioAppComponent`) pour recevoir l'horodatage hôte de CoreAudio. Orchestre la chaîne ci-dessus, le transport (position, pause, retour au début), mute/solo du direct et de la boucle, vumètres, waveforms défilantes, capture de calibration. Le callback ne bloque jamais : chargement d'une boucle de projet = `SpinLock` côté message, `TryLock` côté audio (un bloc de silence au pire).
- **`AmpEngine`** — capture `.nam` (NeuralAmpModelerCore), normalisation de niveau d'après la loudness de la capture (−18 dB, comme le plugin NAM : changer de capture ne fait pas sauter le volume), `ToneStack`, IR (`juce::dsp::Convolution`), gain/volume. Swap de modèle thread-safe.
- **`ToneStack`** — Grave / Médium / Aigu / Présence (4 biquads RBJ), sans dépendance JUCE.
- **`Looper`** — mono, temps-réel-safe, sans dépendance JUCE. Sortie de boucle séparée du direct (pour mute/solo), compteur de couches, retour au début, chargement depuis un projet.
- **`Metronome`** — calé sur la position du transport (le 1er temps de la boucle tombe sur le 1er temps de la mesure), premier temps accentué, mixé au monitoring uniquement.
- **`TakeRecorder`** — écrit le mix en WAV via un FIFO sans verrou (`ThreadedWriter`), note l'heure hôte du premier échantillon.
- **`SyncAnalysis`** — détection d'attaque et calcul du décalage entre deux captures horodatées, sans dépendance JUCE.
- **`CameraCapture`** (`_mac.mm`) — AVFoundation direct : liste des caméras (intégrée, USB, iPhone via Continuity Camera), autorisations, aperçu réduit, enregistrement H.264 via `AVAssetWriter`, vignettes, micro de la caméra pour la calibration.
- **`ClipExporter`** (`_mac.mm`) — composition AVFoundation : audio de la prise + vidéo glissée du décalage de synchro, découpe, recadrage 9:16 / 16:9, miroir, titre (rendu CoreText), export `AVAssetExportSession` (H.264/AAC, encodeur matériel).
- **`Project`** — format de projet (§6). **`CaptureLibrary`** — scan des dossiers de captures en arrière-plan, métadonnées des `.nam`.
- **`Session`** — le contrôleur non visuel : périphérique audio (48 kHz / 64 échantillons demandés), projet, prises, calibration, export, réglages persistants (`~/Library/Application Support/Roadie/Roadie.settings`). L'UI ne parle qu'à elle.

**Interface** (`Source/UI/`, d'après le mockup) — `TopBar` (transport, écran mesure/tempo/signature, vumètre, CPU, latence, Exporter), `LibraryPanel` (bibliothèque + recherche), `Timeline` (règle, pistes Guitare/Ampli/Looper/Caméra, tête de lecture, moniteur caméra flottant), `Inspector` (panneau de la piste sélectionnée), `ExportSheet`, `Widgets` et `Icons` (icônes SVG du mockup). `MainComponent` assemble le tout, gère raccourcis, menus macOS et glisser-déposer.

### Sous-modules

- `libs/JUCE` — épinglé sur `9.0.4`. ⚠️ JUCE est sous double licence AGPLv3 / commerciale : une distribution non-AGPL de Roadie nécessitera une licence JUCE commerciale.
- `libs/NeuralAmpModelerCore` — épinglé sur `v0.6.0`. Compilé en bibliothèque **OBJECT** `RoadieNAM` partagée par l'app et les tests (voir §8). Dépendances imbriquées : Eigen, `nlohmann/json`, `AudioDSPTools` (présent mais **pas utilisé** — `juce::dsp::Convolution` pour l'IR).

---

## 4. Utilisation

1. **Ampli** : choisir une capture dans la bibliothèque à gauche (dossiers scannés : `~/Music/Roadie/Captures` et `~/Music`, `+` pour en ajouter ; on peut aussi glisser-déposer un `.nam` sur la fenêtre). Régler Gain, Grave, Médium, Aigu, Présence, Volume ; charger une IR de baffle (`.wav`, ou glisser-déposer).
2. **Looper** : `R` ou ⟲ démarre l'enregistrement, `R` ferme la boucle, `R` encore = overdub. La règle affiche la longueur de la boucle en mesures.
3. **Caméra** : piste Caméra → choisir la caméra → Activer. Le moniteur flottant se déplace par sa barre, double-clic pour l'agrandir.
4. **Calibrer** (une fois par combinaison carte son + caméra) : Calibrer, puis un accord étouffé bien sec (ou un clap près d'un micro de la carte son).
5. **Prise** : ● ou `T` enregistre le mix entendu (sans le clic) + la vidéo ; `T` pour arrêter. Chaque prise est enregistrée immédiatement dans le projet.
6. **Exporter** (`⌘E`) : choisir la prise, le format, la découpe, le titre → MP4 dans `exports/` du projet ; ou audio seul en WAV.

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
├── project.json   tempo, signature, métronome, ampli (capture, IR, potards), caméra, liste des prises
├── loop.wav       la boucle, WAV 24 bits mono
├── takes/         take-N.wav (le mix), take-N.mov (la caméra), take-N/ (vignettes)
└── exports/       les clips rendus
```

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
| Panneau Ampli / Looper / Caméra | 1 / 2 / 3 |
| Exporter | ⌘E |
| Nouveau / Ouvrir / Enregistrer / Enregistrer sous | ⌘N / ⌘O / ⌘S / ⇧⌘S |
| Réglages audio | ⌘, |
| Thème clair / sombre | ⇧⌘L (menu Affichage) |
| Tempo | glisser sur l'écran Tempo (Maj = fin), double-clic pour saisir |
| Réinitialiser un potard | Double-clic |

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

Mesures NAM sur ce Mac (i5-1038NG7, `RoadieNamBench`, 48 kHz) : captures Fender « Slimmable » ~17 % d'un cœur à 64 échantillons (19 % à 32, 16 % à 256) ; WaveNet standard ~21 % à 64. Nettement plus que sur le PC Linux (6-8 %), mais le buffer de 64 reste confortable.

---

## 9. Pour la suite

1. **Tester avec la Scarlett sur le Mac** : latence réelle, calibration de la synchro avec un accord étouffé, prises longues.
2. **Caméra hors macOS** : backend Media Foundation (Windows) et V4L2/GStreamer (Linux) derrière `CameraCapture` ; export équivalent (Media Foundation / FFmpeg).
3. **Polices de la DA** : embarquer Plus Jakarta Sans + Poppins (OFL) via `juce_add_binary_data` si on veut l'identité exacte du mockup hors macOS.
4. **Distribution** : signature Developer ID + notarisation, et licence JUCE (AGPL ou commerciale).
5. **Idées** : plusieurs boucles / pistes de looper, quantification de la longueur de boucle à la mesure, décompte avant enregistrement, choix d'une partie de la boucle dans l'export.
