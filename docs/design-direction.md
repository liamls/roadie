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

Implémentée dans `Source/Theme.h` (révision 2, octobre 2026). Proposition détaillée, avec le benchmark Logic Pro / GarageBand / Ableton Live et un aperçu de l'écran : **https://claude.ai/artifact/VvXv1geVkQMfh5bGUWWTLS** (lien privé, même remarque que pour la maquette).

Ce que le benchmark des trois apps a montré, et qui fonde cette révision :

- **Chrome neutre, couleur réservée au contenu** : panneaux en gris quasi sans teinte, couleur seulement sur les régions, clips et états. Roadie garde ce principe avec sa couleur par module.
- **Paliers de surface marqués** : environ +16 en sRGB par niveau chez Logic et GarageBand (`#18` → `#28` → `#38`). Les paliers de la v1 (environ +8) faisaient se confondre les panneaux.
- **La couleur dit le type** (GarageBand : vert instrument, bleu audio, jaune Drummer) : c'est le levier « débutant ». Les accents v1 étaient les valeurs exactes de Tailwind (teal-400, violet-400…) ; ils sont recalibrés en OKLCH à luminosité proche.
- **Rouge = enregistrement, et rien d'autre** (Logic : rouge pur). Le `rec` v1 était un corail trop proche de l'orange de l'ampli.

Les neutres v1 (`#16171B`…) tiraient en réalité vers le bleu malgré l'intention « chaude » ; ceux de la v2 ont une vraie teinte chaude, très légère (OKLCH h entre 67° et 85°, chroma ≤ 0,008 pour les surfaces et ≤ 0,011 pour les textes).

| Token | Hex | Usage |
|---|---|---|
| `background` | `#181613` | Fond principal, anthracite chaud, jamais noir pur |
| `surface` | `#23201D` | Cartes niveau 1 |
| `surfaceRaised` | `#2F2C28` | Cartes niveau 2 / composants surélevés |
| `surfaceSunken` | `#100E0C` | Champs creux (afficheurs, barres) |
| `border` | `#3D3A36` | Contours |
| `text` | `#F6F3ED` | Texte principal, blanc cassé chaud (16:1 sur le fond) |
| `textMuted` | `#B0ACA5` | Texte secondaire (8:1) |
| `textFaint` | `#87827D` | Texte tertiaire (4,8:1, au-dessus du seuil de 4,5:1 des petits textes) |

**Couleur fixe par module** (jamais une palette libre par piste — trop pour un public non-initié) :

| Module | Hex | Token |
|---|---|---|
| 🟠 Ampli | `#F9853B` | `ampAccent` |
| 🟢 Looper | `#47DAB4` | `loopAccent` |
| 🟣 Caméra | `#B494F5` | `camAccent` |
| 🟡 Export | `#FBD452` | `exportAccent` — seule couleur d'action principale |

Sémantique (distincte de l'accent de couleur, ne jamais réutiliser pour autre chose) :

| Rôle | Hex | Token |
|---|---|---|
| OK / validé | `#61D779` | `ok` |
| Avertissement | `#FABB41` | `warn` — réservé aux jauges CPU / latence, pour ne pas se confondre avec Export |
| Enregistrement actif | `#F73A37` | `rec` |

Texte et icônes posés sur un aplat d'accent (bouton plein, en-tête de région sélectionnée, logo) : `onAccent` `#100E0C`, foncé dans les deux thèmes (13:1 sur le jaune, 5:1 sur le rouge, là où du blanc n'atteindrait que 3,7:1).

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
| `background` | `#181613` | `#F1ECE4` |
| `surface` | `#23201D` | `#F9F6F1` |
| `surfaceRaised` | `#2F2C28` | `#FEFCF9` |
| `surfaceSunken` | `#100E0C` | `#E5DFD6` |
| `border` | `#3D3A36` | `#D8D2C7` |
| `text` | `#F6F3ED` | `#221D18` |
| `textMuted` | `#B0ACA5` | `#69625B` |
| `textFaint` | `#87827D` | `#857F77` |

En clair, `textMuted` atteint 5,1:1 sur le fond et `textFaint` 3,4:1 (contre 2,6:1 en v1).

**Texte et icônes en couleur d'accent** (libellé « Caméra », pastilles d'indication, icônes de piste et de boutons, coches, messages d'état) : passer par `roadie::theme::ink (accent)`. En sombre il rend l'accent tel quel ; en clair, une « encre » assombrie à 4,5:1 ou plus sur le crème, où les accents vifs disparaîtraient (le jaune Export surtout). Les aplats, contours, jauges et formes d'onde gardent l'accent lui-même.

| Accent | Encre (mode clair) |
|---|---|
| `ampAccent` | `#AF4B00` |
| `loopAccent` | `#097964` |
| `camAccent` | `#764EB7` |
| `exportAccent` | `#896600` |
| `ok` | `#217937` |
| `warn` | `#945F00` |
| `rec` | `#CC2827` |

La zone d'image du moniteur caméra reste sombre dans les deux thèmes (c'est un « écran »).
