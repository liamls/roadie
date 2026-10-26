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
| `surfaceRaised` | `#242730` | Cartes niveau 2 / composants surélevés |
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

**Pas encore implémenté** — l'appli utilise actuellement la police système par défaut de JUCE. Direction cible (cf. recherche design, cohérente avec la constante « grotesque géométrique arrondie » dominante relevée sur les apps créateur/musique) :

- **Interface** (labels, valeurs, listes) : grotesque géométrique arrondie et chaleureuse — type *Inter*, *Plus Jakarta Sans* ou *General Sans*
- **Titres / wordmark** : display bold légèrement condensée et ronde — type *Poppins* ou *Gilroy*
- **Chiffres** (BPM, charge CPU, durée) : variante tabulaire pour rester lisibles en jouant

Pour embarquer une police custom dans JUCE : convertir en binaire via `juce_add_binary_data` (CMake) puis charger avec `juce::Typeface::createSystemTypefaceFor()`. Attention aux licences des polices choisies avant de les embarquer dans un binaire distribué.

## Formes & composants

- Rayons de bordure modérés mais généreux : 12-16px sur cartes/boutons — déjà appliqué (`RoadieLookAndFeel`, rayons 8-16px selon le composant)
- **Potards** : anneau de progression conique dans la couleur du module + pointeur, ombre douce — implémenté dans `RoadieLookAndFeel::drawRotarySlider` (actuellement uniquement en orange/`ampAccent`, à paramétrer par couleur de module si on ajoute des contrôles Looper/Caméra avec potards)
- VU-mètres / waveform en dégradé de la couleur du module plutôt qu'en vert/rouge classique de console
- Icônes au trait, épaisseur moyenne, terminaisons arrondies — registre sobre, pas d'emoji ni d'illustration enfantine
- Navigation par **pages** (Ampli · Looper · Caméra · Export), chaque page héritant de la couleur de son module pour l'accent (barre active, focus ring) — **pas encore implémenté**, l'UI actuelle empile les cartes verticalement dans une seule fenêtre plutôt que de naviguer entre pages

## Mode clair/sombre

Sombre par défaut (confort en conditions de jeu/scène, contraste webcam). Mode clair prévu plus tard (Jalon 6) : mêmes 4 couleurs de module, fond inversé vers un crème chaud plutôt qu'un blanc pur, pour rester dans l'esprit « backstage » et non « bureau ». **Pas implémenté** — `RoadieLookAndFeel` ne gère qu'un thème pour l'instant.
