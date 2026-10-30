# Format de projet

Un projet est un dossier `.roadie` lisible dans le Finder.

Un projet est un **dossier** `<Nom>.roadie/` (dans `~/Music/Roadie/Projets` par défaut) :

```
Riff du soir.roadie/
├── project.json   tempo, signature, métronome, ampli (capture, IR, potards), caméra, prises, arrangement
├── loop.wav       la boucle, WAV 24 bits mono
├── song/          le morceau d'accompagnement, copié tel qu'importé
├── takes/         take-N.wav (la guitare amplifiée), take-N.mov (la caméra), take-N/ (vignettes)
└── exports/       les clips rendus
```

**Version 2** (montage) : `project.json` porte un objet `arrangement` — les régions (`clips` : prise *par son nom*, `start` sur la timeline, `sourceStart` dans la prise, `length`, en secondes), la place de la boucle (`loopAnchor`, `loopStart`, `loopEnd`, < 0 = jusqu'à la fin), la zone de cycle (`cycleOn`, `cycleStart`, `cycleEnd`) et le volume des pistes en dB. Un projet en version 1 est migré à l'ouverture : ses prises sont posées bout à bout, chacune calée sur une mesure.

Ajouts compatibles (les anciens projets s'ouvrent tels quels) : `song` (fichier dans `song/`, titre, tempo détecté et retenu, `firstDownbeat`, durée, parties, vitesse, transposition, volume), `arrangement.songStart` (annulable par ⌘Z), `arrangement.masterVolumeDb`, et dans `amp` la noise gate (`gateOn`, `gateThresholdDb`) et les contournements (`bypassCapture`, `bypassEq`, `bypassCab`).

Pourquoi un dossier : les médias lourds restent hors du JSON, tout reste lisible avec le Finder, et les prises s'écrivent directement à leur place pendant l'enregistrement (rien à copier au moment de sauvegarder). Chemins internes relatifs ; capture et IR en absolu (elles vivent dans la bibliothèque de l'utilisateur). Le projet est sauvegardé automatiquement ~3 s après chaque modification (au plus 30 s sous modifications continues, jamais pendant une prise ou un export), après chaque prise et à la fermeture ; chaque sauvegarde garde la précédente dans `project.json.bak`, que Roadie rouvre si `project.json` est illisible ; `⌘S`, `⇧⌘S` (enregistrer sous, copie le dossier), `⌘O`, `⌘N` ; un dossier `.roadie` se rouvre aussi par glisser-déposer.
