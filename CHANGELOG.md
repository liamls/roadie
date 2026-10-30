# Journal des changements

Format inspiré de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/). Les versions suivent `MAJEUR.MINEUR.CORRECTIF`, avec `-beta.N` pendant la bêta.

## [Non publié]

### Ajouté
- **Sauvegarde automatique** ~3 s après chaque modification (au plus 30 s sous modifications continues, jamais pendant une prise ou un export).
- **Copie de secours du projet** (`project.json.bak`) : rouverte automatiquement si `project.json` est illisible ; messages d'erreur distincts.
- **Import YouTube** : progression du téléchargement, messages d'erreur précis (vidéo privée, indisponible, pas de connexion, délai dépassé).
- **Distribution macOS** : `Tools/package_macos.sh` (runtime renforcé, notarisation, DMG), yt-dlp épinglé par version et SHA-256, `THIRD_PARTY_NOTICES.md`.
- CI GitHub (tests Linux + build et tests macOS arm64 / Intel) et workflow de release.

### Modifié
- Roadie est publié sous **AGPLv3**.
- La documentation est répartie dans `docs/` ; le README devient une page d'accueil.

## Avant la bêta

L'historique antérieur (prototype audio, looper, caméra synchronisée, export de clip, interface DAW, montage, espaces Jouer / Monter, morceaux d'accompagnement) est résumé dans la [roadmap](ROADMAP.md).
