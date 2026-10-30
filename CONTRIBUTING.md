# Contribuer à Roadie

Merci de t'y intéresser. Roadie est un projet jeune (bêta) : les retours d'un guitariste qui s'en sert valent autant que le code.

## Avant de commencer

- **Signaler un bug ou proposer une idée** : ouvre une [issue](https://github.com/liamls/roadie/issues/new/choose). Pour un bug, indique ta version, ta machine, ton interface audio et ta caméra.
- **Faille de sécurité** : ne l'ouvre pas en public, voir [SECURITY.md](SECURITY.md).
- **Licence** : toute contribution est publiée sous [AGPLv3](LICENSE), comme le reste du projet.

## Compiler et tester

```bash
git clone --recursive https://github.com/liamls/roadie.git && cd roadie
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Roadie RoadieLooperTests RoadieDspTests RoadieEngineTests -j
ctest --test-dir build --output-on-failure
```

Toujours en `Release` (en Debug, le moteur NAM sature le CPU). Détails et outils de vérification de l'interface (`--snapshot`, `--selftest-edit`) : [docs/building.md](docs/building.md).

## Règles du dépôt

- **Le thread audio ne bloque jamais** : pas d'allocation, de verrou bloquant ni d'entrée/sortie dans le callback. Les échanges avec l'interface passent par des FIFO, des atomiques ou des `TryLock`.
- **Un test pour chaque comportement** : la logique sans JUCE (looper, DSP, règles pures) va dans `Tests/LooperTests.cpp` ou `Tests/DspTests.cpp` (quelques secondes, sans dépendance) ; ce qui touche la chaîne temps réel, les fichiers ou l'export va dans `Tests/EngineTests.cpp`.
- **L'interface ne parle qu'à `Session`** : toute édition de l'arrangement passe par `applyEdit` (annulable). Voir [docs/architecture.md](docs/architecture.md).
- **Tout texte visible est en français**, avec ses accents. Majuscules de libellés via `toUpperFrench`, jamais `toUpperCase()` (il abîme les accents sur macOS).
- **Style** : suis le code voisin (nommage, densité de commentaires). Les commentaires expliquent le *pourquoi*.
- **Vérifier à l'œil** : pour un changement d'interface, joins une capture (`Roadie.app --snapshot=/tmp/ui.png --demo`), claire et sombre si le thème est concerné.

## Commits et pull requests

- Message : `<gitmoji>(<portée>) <résumé à l'impératif ou au passé>`, par exemple `🐛(timeline) la tête de lecture ne saute plus au zoom`. Gitmojis courants : ✨ fonction, 🐛 correction, ♻️ refactor, 🧪 tests, 📝 doc, 🔧 config, 🚀 release, ⬆️ dépendances.
- Une PR = un sujet. Remplis le modèle de PR ; la CI doit être verte (`CI OK`). Les PR sont fusionnées en **squash**.
- Pas de binaire ni de capture `.nam` / IR dans le dépôt (chaque capture appartient à son auteur).

## Publier une version (mainteneurs)

[docs/releasing.md](docs/releasing.md).
