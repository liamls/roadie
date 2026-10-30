# Architecture

Comment Roadie est construit, et pourquoi.

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
- **`Project`** — format de projet ([project-format.md](project-format.md)). **`CaptureLibrary`** — scan des dossiers de captures en arrière-plan, métadonnées des `.nam`.
- **`Session`** — le contrôleur non visuel : périphérique audio (48 kHz / 64 échantillons demandés), projet, prises, transport en secondes, **arrangement** (toute édition passe par `applyEdit` : 200 niveaux d'annulation, protégés quand une nouvelle boucle a été enregistrée entre-temps), sélection, couper / supprimer / dupliquer / insérer une prise, cache audio des prises (crêtes à 100 par seconde), mixage hors ligne (`renderMix`), calibration, export d'une plage, réglages persistants (`~/Library/Application Support/Roadie/Roadie.settings`). L'UI ne parle qu'à elle.

Ajouts de la refonte : **`SongAnalysis`** (tempo par autocorrélation d'une enveloppe d'attaques sur trois bandes puis ajustement d'une grille sur tout le morceau, premier temps par la grosse caisse, parties par nouveauté mesure à mesure), **`TimeStretch`** (WSOLA hors ligne sur les deux canaux à la fois, puis rééchantillonnage pour transposer), **`NoiseGate`** (avant la capture, hystérésis de 6 dB), **`Tuner`** (YIN) — tous sans JUCE. `AudioEngine` mixe en **stéréo** (guitare, boucle et clic au centre, le morceau avec sa stéréo, puis le Master) et place prises, boucles et overdubs une latence aller-retour plus tôt (celle annoncée par l'interface, plus la correction des Réglages audio). Le changement de capture NAM passe par un pointeur rendu au fil principal : plus de bloc sans ampli.

**Interface** (`Source/UI/`) — deux espaces dans une fenêtre, choisis par le sélecteur **Jouer | Monter** en haut à gauche (⌘1 / ⌘2) : `TopBar` (sélecteur, transport, écran mesure/tempo/signature, métronome, accordeur, nom du projet et son état, une pastille latence · CPU qui ouvre les réglages audio, Exporter), `LibraryPanel` (onglets Captures / Morceaux / Prises + recherche), `Monitor` (moniteur intégré : direct avec guides 9:16 / 16:9 et décompte en surimpression en Jouer, lecture seule en Monter), `StagePanel` (Jouer : pédale Looper et balance Morceau / Guitare / Boucle), `Timeline` (bande vidéo en haut, Guitare, Looper, Morceau avec ses parties, Master ; hauteur de chaque piste réglable), `Inspector` (chaîne de signal de l'ampli, Looper, Caméra avec calibration en assistant, Morceau ; empilé et défilant dans la colonne étroite de Monter), `ExportSheet`, `Widgets` (dont `Fader` à courbe de console, `Meter` avec crête et témoin de saturation, `SegmentedControl`, `Splitter` repliable) et `Icons`. `MainComponent` place les panneaux de l'espace courant (tailles par espace, repli, panneau agrandi, transition de 200 ms), coupe la caméra en Monter et route raccourcis, menus et glisser-déposer.

### Organisation du dépôt

| Chemin | Contenu |
|---|---|
| `Source/` | Moteur (sans UI), session, formats ; `Source/UI/` pour les panneaux ; `*_mac.mm` pour AVFoundation, `Platform_stub.cpp` pour les autres systèmes |
| `Tests/` | Tests du looper, du DSP et du moteur ; `TestMovie_mac.mm` fabrique une vidéo synthétique |
| `Tools/` | `nam_bench.cpp`, mesure de charge CPU d'une capture |
| `docs/` | `design-direction.md` (direction visuelle), `reference-ui-garageband-filmcraft.md` (étude d'ergonomie GarageBand / filmcraft), `screenshot.png` |
| `site/` | `index.html`, page de présentation autonome (aucune dépendance au build) |
| `libs/` | Sous-modules JUCE et NeuralAmpModelerCore |

### Sous-modules

- `libs/JUCE` — épinglé sur `9.0.4`. ⚠️ JUCE est sous double licence AGPLv3 / commerciale : Roadie est publié sous **AGPLv3** (`LICENSE`) ; une distribution fermée nécessiterait une licence JUCE commerciale.
- `libs/NeuralAmpModelerCore` — épinglé sur `v0.6.0`. Compilé en bibliothèque **OBJECT** `RoadieNAM` partagée par l'app et les tests (voir « Décisions techniques »). Dépendances imbriquées : Eigen, `nlohmann/json`, `AudioDSPTools` (présent mais **pas utilisé** — `juce::dsp::Convolution` pour l'IR).

## Décisions techniques notables

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
