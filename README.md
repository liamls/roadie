# Roadie

> Appli guitare tout-en-un : ampli basé sur des captures NAM, looper, et (bientôt) caméra synchronisée — pour filmer, jouer et exporter un clip sans jongler entre un DAW, OBS et un simulateur d'ampli.

Nom provisoire. POC en cours de construction. Ce document existe pour que n'importe quelle machine (ou n'importe quelle session Claude) qui récupère ce dépôt ait tout le contexte nécessaire pour continuer, sans dépendre des notes Obsidian du développeur (qui ne suivent pas le dépôt).

---

## 1. État du projet (26/10/2026)

| Jalon | Statut |
|---|---|
| 0 — Préparation | ✅ Clos |
| 1 — Prototype audio | ✅ Clos (carte son, NAM, IR, latence, CPU) |
| 2 — Maquette design | ✅ Clos (direction visuelle définie, mockup HTML interactif) |
| 3 — Looper | 🟡 Looper + métronome faits et testés. Export projet complet pas tranché (format ouvert) |
| 4 — Webcam + synchro | 🔴 **Bloqué sur Linux** — voir §4 |
| 5 — Export clip | 🟡 Export WAV de la boucle fait. Export vidéo bloqué (dépend du Jalon 4) |
| 6 — Interface finale | ⚪ Pas commencé — l'UI actuelle est fonctionnelle mais minimale (deux cartes empilées), pas encore l'interface façon DAW du mockup |

L'appli compile et tourne (testée sur Linux/PipeWire). Chaîne audio temps réel validée : Scarlett → gain → NAM → IR → volume → sortie, ~10 ms de latence aller-retour, 0 craquement.

---

## 2. Build

### Dépendances

**Linux** (testé sur Ubuntu 24.04) :
```bash
sudo apt-get install -y cmake libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev \
  libfreetype6-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libfontconfig-dev libxi-dev
```
(`build-essential` doit déjà être installé pour g++/gcc.)

**macOS** : Xcode + CMake (`brew install cmake`). Pas encore testé sur cette machine — c'est justement l'objet du passage sur Mac.

**Windows** : Visual Studio 2022 + CMake. Pas encore testé.

### Compiler

```bash
git submodule update --init --recursive   # récupère JUCE + NeuralAmpModelerCore (et leurs propres sous-modules)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Roadie -j
```

⚠️ **Toujours compiler en `Release`**. En Debug, le moteur NAM (calcul matriciel Eigen) tourne sans optimisation et sature le CPU — c'est la première cause de craquements qu'on a rencontrée. `-O3` seul (sans LTO, voir §5) suffit largement.

Binaire : `build/Roadie_artefacts/Release/Roadie` sur Linux. Sur Mac, Xcode génère probablement un bundle `.app` au même niveau — à vérifier.

### Outils de vérification (pas besoin de JUCE)

- `Tools/nam_bench.cpp` — benchmark de charge CPU d'une capture NAM à différentes tailles de buffer. Utile pour retrouver la bonne taille de buffer sur une nouvelle machine :
  ```bash
  g++ -std=gnu++20 -O3 -DNAM_SAMPLE_FLOAT=1 \
    -Ilibs/NeuralAmpModelerCore -Ilibs/NeuralAmpModelerCore/Dependencies/eigen -Ilibs/NeuralAmpModelerCore/Dependencies/nlohmann \
    Tools/nam_bench.cpp libs/NeuralAmpModelerCore/NAM/*.cpp libs/NeuralAmpModelerCore/NAM/wavenet/*.cpp \
    -o /tmp/nam_bench
  /tmp/nam_bench chemin/vers/capture.nam 64 44100
  ```
- `Tests/LooperTests.cpp` — teste la logique du looper (record/play/overdub/undo/clear) indépendamment de JUCE :
  ```bash
  g++ -std=c++20 -O2 -ISource Tests/LooperTests.cpp Source/Looper.cpp -o /tmp/looper_test && /tmp/looper_test
  ```
  À relancer après toute modification de `Source/Looper.cpp`.

---

## 3. Architecture

```
Guitare → AmpEngine (gain → NAM → IR → volume) → Looper (enregistre/rejoue) → Metronome (clic, monitoring only) → sortie
```

- **`Source/MainComponent`** — `juce::AudioAppComponent`, possède le device audio et l'UI, orchestre la chaîne ci-dessus dans `getNextAudioBlock`.
- **`Source/AmpEngine`** — charge une capture `.nam` (NeuralAmpModelerCore) et une IR de cabinet (`juce::dsp::Convolution`), applique gain d'entrée/volume de sortie, mesure le niveau crête. Swap de modèle thread-safe (chargement hors thread audio, lecture protégée par `SpinLock` + `TryLock` côté callback — jamais de blocage du thread audio).
- **`Source/Looper`** — looper mono temps-réel-safe, **volontairement sans dépendance JUCE** (seulement `std::`) pour rester testable en isolation. Buffer pré-alloué à `prepare()`, jamais réalloué. Les transitions d'état (Record/Overdub/Undo/Clear) sont demandées depuis le thread UI via des triggers atomiques et appliquées sur le thread audio en tout début de `process()`.
- **`Source/Metronome`** — clic synthétisé, mixé au monitoring uniquement, jamais enregistré dans la boucle.
- **`Source/RoadieLookAndFeel` + `Source/Theme.h`** — direction visuelle (voir `docs/design-direction.md`).

### Sous-modules

- `libs/JUCE` — épinglé sur `9.0.4`
- `libs/NeuralAmpModelerCore` — épinglé sur `v0.6.0`. Les sources `NAM/*.cpp` et `NAM/wavenet/*.cpp` sont compilées directement dans la cible (pas de `add_subdirectory`, le `CMakeLists.txt` racine du dépôt NAM ne définit pas de cible réutilisable). Dépendances imbriquées : Eigen et `nlohmann/json` (sous `Dependencies/`), `AudioDSPTools` (présent mais **pas utilisé** — on a préféré `juce::dsp::Convolution` pour l'IR, plus simple et plus cohérent avec le reste du code JUCE).

---

## 4. Blocage connu : caméra sur Linux

JUCE (`juce_video` module) n'a **aucun backend caméra pour Linux** — seulement macOS (`AVFoundation`), iOS, Windows (`DirectShow`/Media Foundation) et Android. Vérifié dans les sources JUCE 9.0.4 : `libs/JUCE/modules/juce_video/native/` ne contient que `juce_CameraDevice_mac.h`, `_ios.h`, `_windows.h`, `_android.h` — pas de variante Linux.

Conséquence : le Jalon 4 (webcam + synchro) et la partie vidéo du Jalon 5 (export) n'ont pas pu être développés ni testés sur la machine de dev Linux. Classe `juce::CameraDevice` à utiliser une fois sur Mac/Windows — API : `CameraDevice::getAvailableDevices()`, `CameraDevice::openDevice(...)`, `createViewerComponent()` pour la prévisualisation.

---

## 5. Décisions techniques notables

| Décision | Raison |
|---|---|
| Pas de LTO (`juce_recommended_lto_flags` retiré du `CMakeLists.txt`) | ~35 min de lien pour un gain marginal sur ce projet (beaucoup de templates Eigen/NAM) ; `-O3` seul suffit |
| `NAM_SAMPLE_FLOAT` défini | Les buffers audio JUCE sont en `float` ; sans ça, NAM utilise `double` par défaut et il faudrait convertir à chaque bloc |
| Buffer audio forcé à 64 échantillons au démarrage | Défaut système (PipeWire) mesuré à ~81 ms de latence round-trip ; le moteur NAM ne coûte que 6-8 % CPU même à 32 échantillons (voir `Tools/nam_bench.cpp`), donc rien ne justifiait un buffer aussi gros. Réglable par l'utilisateur via le bouton Réglages |
| Découpage défensif dans `AmpEngine::process` (jamais plus de `currentMaxBlockSize` échantillons passés à NAM/Convolution en un appel) | Bug réel trouvé : certains backends audio (ex. PipeWire en récupération d'xrun) peuvent agrandir la taille de bloc sans rappel de `prepareToPlay`. NAM/Convolution ont des buffers internes dimensionnés au maximum annoncé à `Reset()`/`prepare()` ; dépasser cette taille écrit hors limites. Les `assert()` qui l'auraient détecté sont désactivés en Release (`NDEBUG`) |
| IR via `juce::dsp::Convolution` plutôt que `ImpulseResponse` d'AudioDSPTools | API plus ancienne (`double**`, hérite de `dsp::History`), moins cohérente avec le reste du code JUCE/`float` |
| `Looper` sans dépendance JUCE | Testable en isolation avec un simple `g++`, sans compiler tout le projet — a permis de trouver et corriger un bug de thread-safety (`undoAvailable` en `bool` simple au lieu d'`atomic<bool>`) avant même de lancer l'UI |

---

## 6. Contrôles

| Action | Raccourci |
|---|---|
| Lecture / pause de la boucle | Espace |
| Enregistrer / stop | R |
| Overdub | O |
| Annuler (un niveau) | U |
| Effacer la boucle | Maj + Retour arrière |
| Réinitialiser un potard / le tempo | Double-clic |

---

## 7. Pour la suite (sur Mac)

1. **Caméra** (Jalon 4) — implémenter `juce::CameraDevice`, prévisualisation live, calibration de synchro (clap/flash) avec l'audio. C'est la raison principale du passage sur Mac.
2. **Interface finale** (Jalon 6) — l'UI actuelle (deux cartes empilées, Ampli + Looper) est fonctionnelle mais loin du mockup. Voir `docs/design-direction.md` pour la DA complète et le lien vers le mockup interactif. Probablement : navigation par pages (Ampli / Looper / Caméra / Export), polices personnalisées (actuellement police système par défaut), moniteur caméra flottant.
3. **Export vidéo** (Jalon 5) — une fois la caméra en place, export MP4 via FFmpeg (encodeur matériel), formats vertical/horizontal, découpe.
4. **Décider le format de projet** (dossier + JSON ?) — question ouverte depuis le début, jamais tranchée. L'export WAV de la boucle est un pis-aller, pas une vraie sauvegarde de projet.
5. Revalider `Tools/nam_bench.cpp` sur le Mac — le CPU est différent, la taille de buffer optimale pourrait l'être aussi.
