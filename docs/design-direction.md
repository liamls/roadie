# Direction design — « Backstage chaleureux »

Extrait de la recherche design complète (notes Obsidian du développeur, hors dépôt). Ce document contient ce qui est nécessaire pour construire l'UI finale (Jalon 6) sans avoir à rouvrir Obsidian.

## Mockup interactif

Maquette HTML interactive (écran principal façon DAW : bibliothèque, pistes colorées par module, moniteur caméra flottant, panneau contextuel) :
**https://claude.ai/code/artifact/747976f8-55b3-41c1-a689-b895727c86f3**

⚠️ Lien privé (appartient au compte Claude du développeur) — à ouvrir avec sa session, pas accessible publiquement.

## Concept directeur

> Une scène sombre et chaude (pas un noir froid de DAW) sur laquelle les couleurs des modules éclatent comme des spots — écho à la métaphore du roadie qui organise le matos dans la pénombre des coulisses.

Positionnement : entre les outils pro (Ableton, Logic, Resolve — sombres, denses, jargon technique) et les outils grand public (GarageBand, Lava Studio — accueillants mais limités). Roadie vise la profondeur technique des premiers avec l'accueil visuel des seconds.

## Palette

Déjà implémentée dans `Source/Theme.h` :

| Token | Hex | Usage |
|---|---|---|
| `background` | `#16171B` | Fond principal, quasi-noir chaud, jamais noir pur |
| `surface` | `#1E2026` | Cartes niveau 1 |
| `surfaceRaised` | `#272A32` | Cartes niveau 2 / composants surélevés (valeur du mockup) |
| `surfaceSunken` | `#121318` | Champs creux (afficheurs, barres) |
| `border` | `#2E313A` | Contours |
| `text` | `#F5F3EE` | Texte principal, blanc cassé chaud |
| `textMuted` | `#9A978F` | Texte secondaire |
| `textFaint` | `#6B6962` | Texte tertiaire |

**Couleur fixe par module** (jamais une palette libre par piste — trop pour un public non-initié) :

| Module | Hex | Token |
|---|---|---|
| 🟠 Ampli | `#FF7A3D` | `ampAccent` |
| 🟢 Looper | `#2DD4BF` | `loopAccent` |
| 🟣 Caméra | `#A78BFA` | `camAccent` |
| 🟡 Export | `#FFD166` | `exportAccent` |

Sémantique (distincte de l'accent de couleur, ne jamais réutiliser pour autre chose) :

| Rôle | Hex | Token |
|---|---|---|
| OK / validé | `#4ADE80` | `ok` |
| Avertissement | `#FBBF24` | `warn` |
| Enregistrement actif | `#E85D4A` | `rec` |

## Typographie

**Implémenté avec Avenir Next** (police système de macOS, `roadie::fonts` dans `Source/Theme.h`) : même famille que la cible ci-dessous, sans fichier à embarquer ni licence à vérifier. Regular/Medium pour l'interface, Demi Bold pour les noms et boutons, Bold pour les titres et grands chiffres ; chiffres tabulaires simulés par `roadie::fonts::drawTabular` (BPM, CPU, chronos). Hors macOS, repli sur la police par défaut — embarquer Plus Jakarta Sans / Poppins si l'identité exacte compte. Direction cible (cf. recherche design, cohérente avec la constante « grotesque géométrique arrondie » dominante relevée sur les apps créateur/musique) :

- **Interface** (labels, valeurs, listes) : grotesque géométrique arrondie et chaleureuse — type *Inter*, *Plus Jakarta Sans* ou *General Sans*
- **Titres / wordmark** : display bold légèrement condensée et ronde — type *Poppins* ou *Gilroy*
- **Chiffres** (BPM, charge CPU, durée) : variante tabulaire pour rester lisibles en jouant

Pour embarquer une police custom dans JUCE : convertir en binaire via `juce_add_binary_data` (CMake) puis charger avec `juce::Typeface::createSystemTypefaceFor()`. Attention aux licences des polices choisies avant de les embarquer dans un binaire distribué.

## Formes & composants

- Rayons de bordure modérés mais généreux : 12-16px sur cartes/boutons — déjà appliqué (`RoadieLookAndFeel`, rayons 8-16px selon le composant)
- **Potards** : anneau de progression conique dans la couleur du module + pointeur, face creusée — `RoadieLookAndFeel::drawRotarySlider`, couleur prise sur le slider (`roadie::ui::Knob` reçoit l'accent de son module)
- VU-mètres / waveform en dégradé de la couleur du module plutôt qu'en vert/rouge classique de console
- Icônes au trait, épaisseur moyenne, terminaisons arrondies — registre sobre, pas d'emoji ni d'illustration enfantine
- ~~Navigation par pages~~ → **écran unique façon DAW, comme le mockup retenu** : bibliothèque à gauche, pistes Guitare / Ampli / Looper / Caméra au centre, inspecteur en bas. Sélectionner une piste (clic ou touches 1/2/3) affiche ses réglages dans l'inspecteur avec la couleur de son module (barre d'accent de la piste et de l'inspecteur). L'export est une fenêtre modale (bouton Exporter jaune)

## Mode clair/sombre

Sombre par défaut (confort en conditions de jeu/scène, contraste webcam). **Mode clair implémenté** (menu Affichage ou ⇧⌘L, mémorisé) : mêmes 4 couleurs de module, fond crème chaud plutôt que blanc pur, pour rester dans l'esprit « backstage » et non « bureau ». Les jetons neutres de `Source/Theme.h` sont commutés à chaud par `roadie::theme::setAppearance()` :

| Token | Sombre | Clair |
|---|---|---|
| `background` | `#16171B` | `#F3EEE5` |
| `surface` | `#1E2026` | `#FBF8F2` |
| `surfaceRaised` | `#272A32` | `#FFFDF9` |
| `surfaceSunken` | `#121318` | `#E9E2D6` |
| `border` | `#2E313A` | `#DCD3C4` |
| `text` | `#F5F3EE` | `#1F1C17` |
| `textMuted` | `#9A978F` | `#6E685E` |
| `textFaint` | `#6B6962` | `#9C9588` |

La zone d'image du moniteur caméra reste sombre dans les deux thèmes (c'est un « écran »).
