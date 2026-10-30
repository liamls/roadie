# Roadmap

**Stade : bêta fermée (macOS).** Le cœur du produit est complet ; il reste à le valider sur du vrai matériel et à le distribuer proprement.

## Ce qui est fait

| Jalon | Contenu |
|---|---|
| Prototype audio | Carte son, captures NAM, IR, latence ~10 ms, charge CPU |
| Looper | Enregistrer → boucler → overdub, annuler, effacer ; métronome calé sur le transport |
| Webcam + synchro | Capture AVFoundation, prises audio + vidéo, calibration de la synchro ([détails](docs/sync-audio-video.md)) |
| Export clip | MP4 H.264/AAC (encodeur matériel), 9:16 ou 16:9, découpe, titre incrusté, audio seul en WAV |
| Interface | Façon DAW : bibliothèque, pistes, tête de lecture, moniteur caméra, inspecteur, thèmes sombre et clair |
| Montage | Régions éditables, cycle, zoom, annuler/rétablir (200 niveaux), panneaux repliables |
| Jouer / Monter | Deux espaces, morceaux d'accompagnement (tempo, parties, ralentir, transposer, import YouTube), chaîne de signal avec noise gate, Master, accordeur, décompte |
| Robustesse | Sauvegarde auto ~3 s après chaque édition, reprise depuis `project.json.bak`, tests moteur (looper, DSP, chaîne temps réel, export) |
| Distribution | Script de packaging (signature, notarisation, DMG), CI et workflow de release GitHub |

## Avant d'ouvrir la bêta

- [ ] **Tester avec une vraie interface audio** (Scarlett) : latence réelle et compensation (une prise jouée sur un clic doit tomber sur la grille), calibration de la synchro, prise longue filmée, vrai morceau, accordeur avec une guitare.
- [ ] Apple Silicon : premier build et premier test réel (la CI compile déjà en arm64).
- [ ] Certificat Developer ID + notarisation (voir [Publier une version](docs/releasing.md)).

## Ensuite

1. **Carte de tempo** : un morceau joué sans clic dérive par rapport à la grille fixe ; une carte de tempo suivrait.
2. **Plusieurs pistes de looper**, quantification de la longueur de boucle à la mesure, choix d'une partie de la boucle dans l'export.
3. **Caméra et export hors macOS** : Media Foundation (Windows), V4L2 / GStreamer (Linux) derrière `CameraCapture`. Le moteur audio et l'interface compilent déjà sur Linux, sans caméra.
4. **Polices de la direction artistique** (Plus Jakarta Sans + Poppins, OFL) embarquées pour retrouver l'identité exacte hors macOS.
5. **Séparer la guitare du morceau d'origine**, l'outil idéal pour les reprises (gros chantier).
