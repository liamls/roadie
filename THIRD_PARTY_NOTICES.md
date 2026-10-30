# Composants tiers

Roadie embarque ou compile les composants suivants.

| Composant | Licence | Où |
|---|---|---|
| [JUCE](https://juce.com) 9.0.4 | AGPLv3 **ou** licence commerciale JUCE | `libs/JUCE` (sous-module), lié dans l'app |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) v0.6.0 | MIT (© 2023 Steven Atkinson) | `libs/NeuralAmpModelerCore`, compilé dans l'app |
| [Eigen](https://eigen.tuxfamily.org) | MPL 2.0 | dépendance de NeuralAmpModelerCore |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | dépendance de NeuralAmpModelerCore |
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) 2026.10.26 | Unlicense (binaire autonome : voir aussi les licences des composants Python qu'il regroupe) | `Roadie.app/Contents/Resources/yt-dlp`, lancé comme processus séparé |

## À savoir avant de distribuer

- **Licence de Roadie : AGPLv3** (voir `LICENSE`), compatible avec l'AGPLv3 de JUCE. Conséquence : toute personne qui reçoit l'application (DMG de la beta compris) doit pouvoir obtenir le code source correspondant. Joindre un lien vers le dépôt dans les notes de la beta, et garder le dépôt à jour de ce qui est distribué. Passer plus tard à une licence JUCE commerciale permettrait de fermer le code (il faudrait alors changer la licence de Roadie, ce qui suppose d'être seul détenteur des droits).
- **Captures `.nam` et réponses d'impulsion** : Roadie ne les embarque pas ; chaque capture appartient à son auteur et a ses propres conditions.
- **yt-dlp** : l'import par lien YouTube télécharge de l'audio depuis un service dont les conditions d'utilisation n'autorisent pas, en général, le téléchargement. À réserver à un usage personnel (travailler un morceau que l'on possède ou dont on a le droit de se servir), et à mentionner dans les notes de la beta. La version embarquée est épinglée et vérifiée par empreinte SHA-256 (voir `CMakeLists.txt`) ; YouTube change souvent, d'où des mises à jour régulières à prévoir.
