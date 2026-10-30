# Compiler Roadie

Dépendances, compilation, tests et outils de vérification.

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
| `RoadieDspTests` | Égaliseur (courbes), métronome (clics, décompte), détection d'attaque et mesure de décalage de la synchro, libellé CPU, **règle de sauvegarde auto** (silence de 3 s, plafond de 30 s, jamais pendant une prise), **analyse de morceau** (tempo, premier temps, parties, tap tempo, sur une batterie synthétique), noise gate, accordeur (YIN), étirement temporel et transposition | non |
| `RoadieEngineTests` | Toute la chaîne temps réel via un faux périphérique audio : boucle, mute/solo, pause, overdub, métronome jamais enregistré, prise WAV + horodatage, **compensation de latence** (prise, boucle, saut du décompte), sortie stéréo / mono, Master et gate, **lecture du morceau** en stéréo, entrée de l'accordeur, capture de calibration, une vraie capture NAM (et ses changements en cours de lecture), aller-retour d'un projet avec morceau, **project.json abîmé** (reprise depuis `project.json.bak`, messages d'erreur distincts), **export MP4** 9:16 / 16:9 / plage de la timeline avec titre incrusté (à partir d'une vidéo synthétique) | oui |

Outils :
- `build/RoadieNamBench <capture.nam> <buffer> <sampleRate>` — charge CPU d'une capture (mesures en bas de [architecture.md](architecture.md)).
- `Roadie.app --snapshot=/tmp/ui.png [--demo] [--demo-song[=fichier]] [--workspace=play|edit] [--library=captures|songs|takes] [--track=amp|loop|camera|song] [--export] [--light|--dark] [--camera] [--scale=0.9] [--no-library] [--no-inspector]` — rend la fenêtre dans un PNG puis quitte : vérifier l'interface sans capture d'écran (le terminal n'a pas le droit d'enregistrer l'écran). `--demo` ouvre un projet de démo (deux prises synthétiques, une boucle, un cycle) pour voir des régions sans rien filmer ; `--demo-song` y importe un morceau synthétique (batterie et basse à 92 BPM), `--demo-song=/chemin/morceau.m4a` un vrai fichier (chemin sans espace : la ligne de commande est découpée aux espaces). La caméra reste éteinte pendant ces vérifications, sauf avec `--camera`. Lancer via `open -W Roadie.app --args …` pour que les autorisations caméra/micro soient demandées au nom de Roadie.
- `Roadie.app --selftest-edit` — ouvre le projet de démo et le monte comme un utilisateur, avec de **vrais événements souris** injectés dans la fenêtre (toute la chaîne JUCE : zones de prise, seuils de glissement, ⌥, double-clic) : déplacer, rogner les deux bords, dupliquer à ⌥, déplacer la boucle, dessiner un cycle, placer la tête dans la règle, ⌘Z ×4 puis ⇧⌘Z, les séparateurs, Y et B ; puis les espaces (⌘1 / ⌘2, caméra coupée en Monter, chaque séparateur de Jouer, repli et réouverture d'un panneau, tailles mémorisées par espace, panneau agrandi, ● depuis Monter avec son décompte), le morceau (import, tempo, calage sur la mesure 2, lecture, M, clic sur une partie, ×2, décalage et ⌘Z, export avec / sans, 75 % et +2 demi-tons), l'armement, les blocs de l'ampli, le Master et la hauteur d'une piste (60 vérifications, ~40 s). Une ligne par vérification sur stderr ; code de sortie = nombre d'échecs. La fenêtre passe au premier plan (info-bulles coupées) : **ne pas toucher la souris pendant les ~5 s du test**. Le robot partage la souris du système : un vrai mouvement au-dessus de la fenêtre, ou une fenêtre qui passe devant (demande d'autorisation, notification), fait échouer des gestes — relancer dans ce cas. Si la fenêtre n'est pas à l'écran du tout (session verrouillée, écran en veille), le robot remet les gestes directement aux composants (mêmes événements, même ordre) et le signale dans le journal : la logique est vérifiée, pas la distribution par macOS.
- `Roadie.app --selftest-take=/tmp/dossier` — ouvre la caméra, enregistre une prise de 3 s (audio + vidéo) dans un projet créé dans ce dossier, l'exporte en MP4 et affiche le résultat sur stderr. **Filme réellement** : supprimer le dossier ensuite.

Ces outils sauvegardent `Roadie.settings` au démarrage et le remettent en place en quittant : thème, tailles des panneaux, dernier projet ouvert… restent ceux de l'utilisateur.
