# Référence UI et fonctionnalités simples — GarageBand et filmcraft

Document autonome, à réutiliser dans n'importe quel projet d'application audio, vidéo ou de montage. Il décrit :

- **A.** comment GarageBand et filmcraft organisent leur interface ;
- **B.** un catalogue de **fonctionnalités simples** (peu de code, fort confort), chacune avec son comportement de référence ;
- **C.** pour mémoire, les fonctionnalités plus ambitieuses repérées ;
- **D.** les preuves et la façon de les retrouver.

Rédigé le 29/10/2026.

## Sources et limites

| | GarageBand | filmcraft |
|---|---|---|
| Version | 10.4.14 (build 6648), `/Applications/GarageBand.app` | commit `8d925e9` du 28/10/2026, <https://github.com/storytold/filmcraft> |
| Nature | Application Apple propriétaire (DAW grand public) | Monteur vidéo libre façon Premiere Pro, en Rust + egui, licence MIT / Apache-2.0 |
| Méthode | Analyse **statique** du paquet : `Info.plist`, frameworks, fichiers d'interface (`.nib`), textes d'interface (`Localizable.strings`, 2 945 libellés), fiches d'aide (`QuickHelp.plist`, 500 fiches), noms de classes Objective-C (2 127). L'application n'a pas été lancée ni décompilée. | Lecture du code source et de la documentation du dépôt |
| Ce qu'on sait | Ce que fait chaque élément d'interface, d'après les textes d'aide livrés par Apple | Ce que fait le code, et comment |
| Réutilisation | **Inspiration ergonomique seulement** : ne copier ni icônes, ni images, ni textes | Idées **et** algorithmes réutilisables en citant la source (MIT / Apache-2.0) |

Les identifiants entre crochets, comme `[TRA_074]`, renvoient aux fiches de `QuickHelp.plist` de GarageBand ; les chemins `crates/…` renvoient au dépôt filmcraft (détails en partie D).

---

## A. Organisation de l'interface

### A1. GarageBand : une seule fenêtre, des zones repliables autour des pistes

```
┌ Barre de contrôle ──────────────────────────────────────────────────────────────────────────┐
│ Bibliothèque · Smart Controls · Éditeurs │ ⏮ ⏪ ⏩ ■ ▶ ● ⟲ │ Écran LCD │ Accordeur · Décompte · │
│                                          │                 │           │ Métronome · Volume     │
│                                          │                 │           │ Note Pad · Loops · Aide│
├ Bibliothèque ┬ Pistes globales (Arrangement, Vidéo, Tempo, Transposition, Master) ┬ Loops  ┤
│ (sons/patchs)│ En-têtes  │ Règle : cycle jaune, locators, tête de lecture         │ Média  │
│              │ de piste  │ Régions (audio, MIDI, Drummer, dossiers de prises)     │ Notes  │
├──────────────┴───────────┴ Smart Controls  ou  Éditeur (audio, piano roll…) ──────┴────────┤
```

**Barre de contrôle** (clés `StrTransportBtns` et `StrViewBtns`)

- **À gauche**, les interrupteurs des zones : Bibliothèque, Smart Controls, Éditeurs.
- **Au centre**, le transport : début, retour d'une mesure, avance d'une mesure, stop, lecture, enregistrement, cycle.
- **L'écran LCD** affiche la position (mesures.temps ou heures:min:s), le tempo, la tonalité et la signature. On change chaque valeur **en la glissant verticalement**, ou on **double-clique pour la saisir** `[TRA_019, TRA_020, TRA_028, TRA_030]`. Un menu bascule l'écran **et** la règle entre mesures et temps `[TRA_071]`.
- **Juste après**, les outils de jeu : accordeur, décompte, métronome, volume général.
- **À droite**, les autres zones : Note Pad, Loop Browser, Media Browser, Aide rapide.

**Pistes globales** (au-dessus des pistes, chacune affichable ou masquable) `[GMT_005 à GMT_009]`

- Arrangement : des marqueurs de sections (Intro, Couplet…).
- Vidéo : des vignettes calées sur la règle.
- Transposition et Tempo.
- Master : le volume général.

**En-tête de piste**, dont on choisit les éléments affichés (`TrackHeaderConfig.nib`, `[GMT_010]`)

- Muet, solo, armement de l'enregistrement, écoute de l'entrée, verrou, volume, panoramique, « piste de groove ».
- Double-clic sur le nom pour renommer `[TRL_054]`, ctrl-clic sur l'icône pour la changer `[TRL_055]`.

**Règle** `[COM_006 à COM_015]`

- Divisions en mesures ou en temps. Un clic dans le **bas** de la règle place la tête de lecture.
- La **zone de cycle** est une bande jaune en haut : on la glisse pour la déplacer et on tire ses bords pour la redimensionner. Ses deux bords sont les **locators** gauche et droit.
- Un **marqueur de fin de projet** recule ou avance tout seul pendant l'enregistrement.

**Régions**

- Types : audio, MIDI, Drummer, **dossiers de prises** (un numéro en haut à gauche permet de choisir la prise) `[ARR_050]`.
- Une partie « bouclée » se dessine différemment de la région d'origine `[ARR_059 à ARR_062]`.
- Le pointeur change selon la zone survolée : redimensionner (bord), **boucler** (coin haut droit, on tire vers la droite), **jonction** (entre deux régions collées, on déplace la transition) `[TOO_029 à TOO_031]`.

**Zone du bas : Smart Controls** `[SMA_001 à SMA_037]`

- Les commandes principales du son de la piste, sous forme de gros potards.
- Un bouton **Piste** et un bouton **Master**. Le Master a trois sections : Effets, Sortie, EQ.
- Un bouton **Comparer** fait entendre le réglage enregistré, un bouton **Bypass** désactive l'effet, et un menu charge ou enregistre des réglages.
- Un volet « plug-ins » sur le côté pour ajouter, retirer ou ouvrir des effets, avec le niveau d'enregistrement et le choix de l'entrée.
- Pour une piste guitare : Amp Designer, Pedalboard, Accordeur, Noise Gate, protection anti-larsen, niveau automatique.

**Bibliothèque** (à gauche) `[LIB_001 à LIB_028]`

- Des catégories, puis la liste des sons, avec une recherche qui garde les recherches récentes.
- Enregistrer le son courant comme nouveau son ; supprimer un son (irréversible, l'aide le dit).
- Un son pas encore téléchargé est grisé, avec une flèche pour le télécharger.

**Loop Browser** (à droite) `[LPB_*]`

- Deux vues : colonnes de mots-clés, ou boutons de mots-clés qu'on combine. Un bouton Réinitialiser et des favoris.
- Préférences : n'afficher que les boucles proches de la tonalité du projet (à ±2 demi-tons près) `[PFS_FilterResultsCheckbox]`, et afficher le tempo et la tonalité d'origine de chaque boucle.

**Aide rapide** `[GMH_*, TRA_069]`, classes `MAQuickHelp*`

- Survoler un élément affiche son nom et ce qu'il fait.
- Deux présentations au choix : **bulles** près du pointeur, ou **fenêtre flottante** déplaçable.
- Des « coaching tips » par grande zone de l'écran.

**Préférences** (un fichier d'interface par onglet)

- Général, Audio, Métronome (volume et timbre du clic), Loops, Avancé, Mes infos (compositeur, artiste, album, utilisés au partage).

### A2. filmcraft : des panneaux qu'on ancre où on veut, et des workspaces

**Barre d'en-tête de 38 pt** (`header.rs`)

- À gauche : Accueil et les modes **Import / Edit / Export**.
- Au centre : le titre du document.
- À droite : le nom du workspace et des actions rapides.
- Sur macOS, les menus sont dans la barre de menus native.

**Workspaces et panneaux** (`dock.rs`)

- 28 panneaux qu'on ancre en onglets ou qu'on laisse flotter.
- 9 workspaces tout prêts : Editing, Assembly, Color, Effects, Audio, Captions and Graphics, Learning, Review, All Panels.
- Menu Workspaces (`panels/workspaces.rs`) : revenir à la disposition enregistrée, enregistrer les changements, enregistrer comme nouveau workspace, renommer ou supprimer.
- Un workspace intégré peut être modifié puis réinitialisé, mais pas supprimé.
- Les dispositions sont dans `workspaces.json`, à côté des préférences.

**Timeline** (`panels/timeline.rs`)

- Un **grand timecode** de la tête en haut à gauche, avec des interrupteurs à côté.
- La règle porte les marqueurs, les points d'entrée et de sortie et une **barre de rendu** (vert, jaune ou rouge).
- Les pistes vidéo sont au-dessus (V1 en bas), les pistes audio en dessous (A1 en haut).
- Les en-têtes sont à gauche, et une barre de défilement en bas sert aussi au zoom.
- Le **zoom et le défilement sont animés** (amortissement exponentiel) et **ancrés sous le pointeur**.
- Chaque geste produit **une seule commande** : il s'annule d'un coup et peut être rejoué.
- Aimantation à **9 px** (`const SNAP_PX`) : sur les coupes, la tête, les marqueurs et les points d'entrée et de sortie. La tête glissée ne s'aimante pas sur elle-même, sinon elle « collerait » sur place.

**En-tête de piste** (`timeline/headers.rs`)

- Cible, patch source, verrou de synchro, verrou, muet, solo, activée ou non, couleur.

**Moniteurs Source et Programme** (`panels/monitor.rs`, `panels/monitor_view.rs`)

- L'image est décodée à la **plus petite** de deux résolutions : celle de lecture et celle de l'écran.
- Pendant un scrub, on affiche l'image en cache la plus proche en attendant la bonne, donc **pas de flash noir**.
- Options d'affichage : grossissement de 10 à 1600 %, guides qu'on tire depuis les règles, aimantation aux guides, aux bords et au centre, damier pour la transparence.

**Panneau Outils** (`panels/tools.rs`)

- Une colonne d'outils. Les outils groupés portent un petit triangle ; clic droit, appui long ou appui-glisser ouvre le groupe.

**Mode Export** (`panels/export_mode.rs`), en trois colonnes

1. Les destinations et la file d'attente.
2. Les réglages : nom, dossier, preset, format, puis des sections Vidéo, Audio, etc.
3. L'aperçu, la plage, la mise à l'échelle, un **résumé avec la taille de fichier estimée**, et les boutons « Envoyer à la file » et « Exporter ».

**Accueil / Import** (`panels/import_mode.rs`)

- De grandes cartes pour importer des médias, et les projets récents.

**Petits panneaux utiles**

- **Historique** et **Marqueurs** (`panels/misc.rs`).
- **Événements** : avertissements et erreurs, filtrables (`panels/events.rs`).
- **Progression** : tâches de fond, avec un bouton Annuler.
- **Timecode** : une ou plusieurs lignes au choix, comme la position, la durée ou le temps restant (`panels/timecode.rs`).
- **Notes du projet**, stockées dans le projet (`panels/notes.rs`).
- **Navigateur de médias** : arborescence et liste ou vignettes, avec **scrub au survol** (`panels/media_browser.rs`).

**Réglages** (`panels/settings.rs`)

- Les catégories à gauche, les champs à droite, et en bas Aide, Réinitialiser, Annuler, OK.
- Les modifications vont dans un **brouillon**, appliqué seulement sur OK.

**Raccourcis clavier** (`panels/shortcuts_dialog.rs`, ⌥⌘K)

- Un clavier dessiné où les touches attribuées sont colorées, et une liste des commandes avec recherche. On clique dans une cellule puis on presse les touches.
- Les conflits sont signalés. On peut gérer des presets et les importer ou les exporter.
- Les modifications s'appliquent tout de suite ; Annuler restaure l'état d'origine.

**Socle technique de l'interface**

- **Jetons de design** : chaque widget lit ses couleurs et ses tailles dans `Tokens`, donc un thème s'applique partout (`theme.rs`).
- **Traduction** : les textes du code restent en anglais et servent de clés ; un catalogue `.tsv` par langue ; un texte non traduit s'affiche en anglais (`i18n.rs`).

### A3. Principes communs, à reprendre tels quels

1. **Un centre temporel** (pistes et règle) **entouré de zones repliables** : une source à gauche, des réglages contextuels en bas ou à droite, une bibliothèque, chacune avec son bouton et son raccourci.
2. **Un inspecteur contextuel** : ce qui est sélectionné décide de ce qu'affiche la zone de réglages.
3. **Toute action est une commande** avec un libellé, un raccourci, un menu et une étape d'annulation. On obtient ainsi gratuitement les libellés « Annuler X », l'historique, les raccourcis modifiables et le pilotage par script.
4. **Des valeurs numériques directement manipulables** : glisser verticalement pour changer, double-cliquer pour saisir, double-cliquer sur un potard pour revenir à la valeur par défaut.
5. **Les couleurs ont un sens fixe** : jaune pour le cycle et les guides d'alignement, rouge pour l'enregistrement, et une couleur par type de piste.
6. **Des retours qui ne clignotent pas** : la meilleure image disponible plutôt qu'un écran noir, et des valeurs lissées.
7. **Une aide là où l'on regarde** : une aide rapide au survol, plutôt qu'un manuel.

---

## B. Catalogue de fonctionnalités simples

Chaque entrée donne le **comportement de référence** et la **source**. Toutes sont « petites » : un composant, une option, ou une commande de plus sur un modèle de données existant.

### B1. Transport et temps

| Fonction | Comportement de référence | Source |
|---|---|---|
| Décompte avant enregistrement | Aucun, 1 mesure ou 2 mesures, avec un interrupteur dans la barre ; option « métronome seulement pendant le décompte » | GB `[TRA_074, GMR_001 à GMR_003]`, « … only During Count-In » |
| Décompte visuel | Un gros compte à rebours en surimpression, plus un **pré-roll** (lecture avant le départ) et un **post-roll** (après la fin) | FC `panels/voiceover.rs` |
| Stop qui revient au début | Stop à l'arrêt ramène la tête au début, ou au locator gauche si le cycle est actif ; Lecture part du locator gauche en mode cycle | GB `[TRA_014, TRA_015]` |
| Reculer ou avancer d'une mesure | Un bouton ou une touche par mesure | GB `[TRA_012, TRA_013]` |
| Affichage mesures ↔ temps | Un menu change l'écran **et** la règle en même temps | GB `[TRA_071]` |
| Suivi de la tête (Catch) | Un interrupteur : la vue suit la tête pendant la lecture et l'enregistrement | GB `[COM_003]` |
| Fin de projet | Un marqueur de fin qui s'allonge pendant l'enregistrement, utilisé comme plage par défaut | GB `[COM_014]` |
| Métronome réglable | Volume et timbre du clic dans les préférences | GB `[PFS_MetronomeVolumeSlider, PFS_MetronomeToneSlider]` |
| Locators au pas | Placer le locator gauche ou droit à la position arrondie de la tête | GB « Set Left/Right Locator by Rounded Playhead » |
| Panneau Timecode | Plusieurs lignes : position, durée, durée entrée-sortie, temps restant | FC `panels/timecode.rs` |

### B2. Enregistrement et entrée

| Fonction | Comportement de référence | Source |
|---|---|---|
| Accordeur | Bouton dans la barre ; accordages Standard, Drop D, Open D, un ton plus bas | GB `[TRA_073, SMA_019]`, clé `LearnToPlayTunerStrings` |
| Écoute de l'entrée | On entend l'entrée d'une piste **non armée**, pour régler les niveaux ou répéter | GB `[INS_023]` |
| Protection anti-larsen | Si un larsen apparaît, l'écoute est **coupée immédiatement**, avec un message ; l'écoute « sans protection » reste possible | GB `[SMA_025]`, `GBFeedbackProtectionPanel.nib` |
| Niveau automatique | Baisse le niveau pour éviter le larsen, le remonte s'il est trop faible | GB `[SMA_017]` |
| Noise gate | Des réglages simples pour réduire le bruit de fond | GB `[SMA_020]` |
| Niveau et source d'entrée | Un curseur de niveau ; un bouton mono ↔ stéréo puis un menu de source | GB `[SMA_016, SMA_018]` |
| Résolution d'enregistrement | Une case (24 bits) qui fixe la résolution des nouveaux fichiers | GB `[PFS_24BitCheckbox]` |
| Accès au micro refusé | Un bouton « Autoriser l'accès » qui ouvre directement le bon panneau des Réglages Système | GB `[PFS_AllowAccessButton]` |

### B3. Montage de régions

| Fonction | Comportement de référence | Source |
|---|---|---|
| Couper à la tête | Coupe les régions sélectionnées à la position de la tête | GB `[LMT_010]`, FC « add edit » ⌘K |
| Joindre | Fusionne des régions contiguës ; message si rien n'est joignable | GB `[LMT_011]`, « no joinable Regions are selected » |
| Rogner jusqu'à la tête | Ramène le début ou la fin de la région à la tête | GB « Trim Region Start/End to Playhead Position » ; FC `Q` / `W` |
| Supprimer en refermant le trou | Supprime la sélection et recule les régions qui suivent | GB `[LMT_008]`, FC « ripple delete » |
| Coller en remplaçant | Colle à la tête en remplaçant ce qui s'y trouve | GB `[LMT_006]` |
| Aimantation activable | Un interrupteur « Aimanter à la grille » ; un seuil de 9 px côté filmcraft | GB `[LMT_012]`, FC `SNAP_PX` |
| Guides d'alignement | Pendant un déplacement, une ligne jaune apparaît quand un bord s'aligne sur une autre région | GB `[LMT_013]` |
| Boucler une région | Coin haut droit, pointeur « boucle », on tire vers la droite ; la partie bouclée a un autre dessin | GB `[TOO_030, ARR_059]` |
| Lecture à l'envers | Une case par région | GB `[AUD_21]` |
| Renommer | Renommer une région ou une piste (double-clic sur le nom) | GB `[CMT_001, TRL_054]` |
| Verrouiller une piste | Une piste verrouillée ne peut plus être éditée ni enregistrée | GB `[TRL_009]`, FC `timeline.track.*.locked` |
| Couleur ou icône de piste | Changées par clic droit (ou ctrl-clic) | GB `[TRL_055, GMT_038]`, FC `timeline.track.*.color` |
| Piste aux réglages dupliqués | Nouvelle piste avec les réglages de la piste sélectionnée | GB `[GMT_002]` |

### B4. Navigation et affichage

| Fonction | Comportement de référence | Source |
|---|---|---|
| Zoom animé sous le pointeur | Animation amortie, et le point sous le pointeur reste fixe | FC `panels/timeline.rs` |
| Curseur de zoom horizontal, zoom vertical | Un curseur dans le coin, plus le zoom vertical des pistes | GB `[COM_018]`, « Vertical Zoom » |
| Zoom de la forme d'onde | Grossir l'amplitude des formes d'onde sans changer le volume | GB « Waveform Zoom » |
| Afficher ou masquer chaque zone | Une commande par zone (bibliothèque, éditeur, automation…), chacune avec un raccourci | GB : 22 commandes « Show/Hide … » |
| Configurer l'en-tête | Choisir les commandes visibles dans les en-têtes de piste | GB `[GMT_010]` |
| Résolution des vignettes vidéo | Haute (par défaut) ou Basse, pour les machines modestes | GB `[PFS_MovieResolutionButtons]` |
| Scrub au survol | Parcourir une vignette en survolant sa largeur | FC `panels/media_browser.rs`, `project_views.rs` |
| Son pendant le scrub | Un court fragment du son est joué à chaque déplacement de la tête ; un seul flux audio est ouvert pour tout le geste | FC `scrub.rs` |
| Pas de flash noir | Pendant un scrub, afficher l'image en cache la plus proche en attendant la bonne | FC `panels/monitor.rs` |
| Panneau maximisé | Une touche agrandit le panneau sous le pointeur, la même le restaure | FC `panels/keyboard.rs` |

### B5. Mixage et sortie

| Fonction | Comportement de référence | Source |
|---|---|---|
| Fondu de fin automatique | Une commande crée un fondu de sortie par automation du master | GB `[GMM_002]` |
| Export normalisé | Exporter au niveau maximal sans distorsion (0 dBFS) | GB `[PFS_AutoNormalizeCheckbox]` |
| Mesure de loudness | BS.1770-4 / EBU R128 : momentanée (400 ms), court terme (3 s), intégrée, true peak | FC `crates/audio-dsp/src/loudness.rs` |
| Crêtes maintenues | Vumètres en dB avec maintien de la crête | FC `panels/meters.rs` |
| Muet, solo, solo protégé | Solo protégé (« Solo Safe », sens déduit du nom) : une piste qui reste audible malgré un solo ailleurs | GB `[TRL_060, TRL_061]` |
| Presets d'export | Groupes (Source, Web & réseaux, Broadcast…) ; « Vertical 1080×1920, remplissage, 12 Mbps » ; favoris ; presets perso, dont les noms intégrés sont réservés | FC `crates/export/src/presets.rs` |
| Taille estimée | Le résumé d'export affiche la taille du fichier avant le rendu | FC `panels/export_mode.rs` |
| Export en tâche de fond | Progression et Annuler, on continue de travailler pendant le rendu | FC `panels/events.rs` (panneau Progression) |
| Métadonnées | Compositeur, artiste, album, saisis une fois dans les préférences | GB `[PFS_MyInfo_01 à 04]` |

### B6. Projet et robustesse

| Fonction | Comportement de référence | Source |
|---|---|---|
| Libellés d'annulation | « Annuler Déplacer la région » plutôt que « Annuler » | GB « Undo %@ » |
| Panneau Historique | Première ligne « Ouverture » (l'état à l'ouverture), puis une ligne par étape ; les étapes à rétablir sont grisées ; cliquer sur une ligne y revient ; un menu propose « Supprimer les étapes à rétablir » et « Effacer l'historique » | FC `panels/misc.rs` |
| Notes du projet | Texte libre enregistré **dans** le projet ; une séance de frappe = une étape d'annulation | GB Note Pad `[TRA_060]`, FC `panels/notes.rs` |
| Sauvegarde auto en versions | Toutes les 5 min s'il y a eu un changement ; garde les 20 dernières copies horodatées | FC `docs/project-files.md` |
| Journal de récupération | Instantané écrit environ 1 s après la dernière édition (au plus toutes les 5 s) ; au redémarrage après un plantage, proposer **Récupérer / Plus tard / Ignorer** | FC `docs/project-files.md` (« Crash recovery ») |
| Écritures atomiques | Projet, sauvegardes auto et préférences : écrire dans un fichier temporaire caché du **même dossier**, `fsync`, renommer par-dessus la cible, puis `fsync` le dossier | FC `docs/project-files.md` (« Saving is atomic ») |
| Journal de plantage | Fil d'exécution, emplacement et pile d'appels écrits dans un fichier ; le dernier plantage est gardé pour une fenêtre d'erreur dans l'application | FC `crash.rs` |
| Panneau Événements | Avertissements et erreurs, filtrables par niveau | FC `panels/events.rs` |
| Réinitialiser les alertes | Un bouton qui réaffiche les alertes masquées par « Ne plus afficher » | GB `[PFS_ResetWarningsButton]` |
| Accueil | Les projets récents, et de grosses cartes « nouveau » ou « importer » | FC `panels/import_mode.rs` |

### B7. Aide et personnalisation

| Fonction | Comportement de référence | Source |
|---|---|---|
| Aide rapide | Bulles au pointeur, ou fenêtre flottante ; une fiche par élément (titre + une phrase) | GB `[GMH_001 à GMH_009]`, `QuickHelp.plist` |
| Éditeur de raccourcis | Clavier dessiné, recherche, conflits signalés, presets, import et export | FC `panels/shortcuts_dialog.rs` |
| Workspaces | Dispositions nommées, enregistrables et réinitialisables | FC `panels/workspaces.rs` |
| Préférences avec brouillon | Les modifications ne s'appliquent que sur OK ; Réinitialiser par catégorie | FC `panels/settings.rs` |
| Jetons de design | Toutes les couleurs et tailles dans un seul objet de thème | FC `theme.rs` |
| Traduction progressive | Le texte anglais sert de clé ; un catalogue `.tsv` par langue ; repli sur l'anglais | FC `i18n.rs` |

---

## C. Plus ambitieux (pour mémoire)

| Fonction | Source |
|---|---|
| Dossiers de prises et assemblage des meilleurs passages, enregistrement en cycle qui empile les prises | GB `[ARR_050, ARR_053]`, classes `NcMultiTakeFolderManager*` |
| Enregistrement automatique sur une plage sélectionnée | GB « Autopunch Range » |
| Marqueurs d'arrangement qu'on réordonne (8 mesures par défaut) | GB `[GLO_026, GLO_028]` |
| Flex Time et Flex Pitch, quantification de l'audio, piste de groove | GB `[TRL_023 à TRL_053, AUD_004 à AUD_009]` |
| Automation en Read / Latch / Touch / Write | GB `[TRL_042]`, README de FC |
| Synchroniser des enregistrements par leur son (corrélation GCC-PHAT en deux passes) | FC `crates/audio-dsp/src/sync.rs` |
| Multicam : grille des angles, touches 1 à 9 pour couper | FC `panels/multicam.rs` |
| Remix : recalibrer une musique à une durée cible en coupant sur les temps | FC `crates/audio-dsp/src/remix.rs` |
| Ducking automatique de la musique sous la voix | FC `crates/audio-dsp/src/ducking.rs` |
| Sous-titres SRT, WebVTT, SCC incrustés ; transcription Whisper | FC `crates/captions`, `crates/speech` |
| Détection des changements de plan dans une vidéo | GB classe `MAMovieSceneCutDetector` |
| Échange de projets : FCPXML, FCP7 XML, OTIO, EDL, AAF | GB classes `MAFinalCutXML*`, FC `crates/interchange` |
| Tout piloter par commandes : plus de 650 commandes, canal JSON, MCP, un identifiant d'automatisation par widget | FC `docs/control-protocol.md`, README |

---

## D. Preuves et comment les retrouver

### GarageBand

- **Identité** : `Info.plist` lu avec REA (preuve `ev_3da38ccd51d77696b1b2085a72ddd70252076ac0392c29022cca792ec5b3debb`). On y lit `com.apple.garageband10`, la version 10.4.14 (6648), les documents `.band`, la classe principale `CLgApplication` et les autorisations micro, Photos et mouvement.
- **Frameworks révélateurs** : `MAVideo` (lecture, vignettes, détection de plans, FCPXML), `MAMixer`, `MALoopBrowser`, `MAPlaySurface`, `MAHarmony`, `MAMachineLearning`, `MAGenerativeInstruments` (Session Players), `MAWorkspace` (rendu des Smart Controls).
- **Fichiers d'interface** dans `Resources/Base.lproj` et `Resources/en.lproj` : `MovieWindow`, `TrackHeaderConfig`, `GBTrackInspector`, `SmartControlsInspector`, `Library`, `GBFeedbackProtectionPanel`, `Prefs{Global,Audio,Metronome,Loops,Advanced,MyInfo}`, `TimeSignatureCustom`, `ExportSongToDiskAccessoryView`.
- **Classes de la table des symboles** : `LgTunerWindowObserver`, `GBFeedbackProtectionPanel`, `RegionMultiTakeFolderManager`, `MovieWindowController`, `MAQuickHelpViewInBalloonWindow`, `CLgSmartControlsInspectorView`.

Pour tout reproduire (lecture seule, à lancer depuis un dossier de travail) :

```bash
R=/Applications/GarageBand.app/Contents/Resources/en.lproj
plutil -convert json -o quickhelp.json "$R/QuickHelp.plist"          # 500 fiches d'aide : titre + texte par élément
plutil -convert json -o strings.json "$R/Localizable.strings"        # 2 945 libellés ; StrTransportBtns|||…, StrViewBtns|||…
ls /Applications/GarageBand.app/Contents/Resources/Base.lproj        # fichiers d'interface (.nib) par écran
nm -arch x86_64 /Applications/GarageBand.app/Contents/MacOS/GarageBand | grep -o 'OBJC_CLASS_\$_[A-Za-z0-9_]*' | sort -u
```

Préfixes des fiches d'aide :
- `TRA` : barre de transport ; `TRL` : en-têtes de piste ; `COM` : règle et zoom ; `ARR` : régions.
- `SMA` : Smart Controls ; `INS` : tranche de console ; `LIB` : bibliothèque ; `LPB` : Loop Browser.
- `LMT` : commandes d'édition ; `TOO` : pointeurs ; `GLO` : pistes globales ; `PFS` : préférences.
- `GM*` : menus (`F` Fichier, `E` Édition, `T` Piste, `M` Mixage, `R` Enregistrement, `S` Partage, `V` Affichage, `H` Aide).

### filmcraft

```bash
git clone --depth 1 https://github.com/storytold/filmcraft.git
```

- **Interface** : `crates/ui-egui/src/` (chaque fichier commence par une explication en `//!`), dont `dock.rs` (panneaux et workspaces), `header.rs`, `panels/*.rs`, `theme.rs` et `i18n.rs`.
- **Traitement du son** : `crates/audio-dsp/src/` (`loudness.rs`, `sync.rs`, `ducking.rs`, `remix.rs`).
- **Projet et sauvegardes** : `docs/project-files.md`.
- **Architecture et commandes** : `docs/architecture.md`, `docs/control-protocol.md`.
