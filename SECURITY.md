# Sécurité

Roadie lit des fichiers venus de l'extérieur (captures `.nam`, réponses d'impulsion, morceaux audio, projets `.roadie`, vidéos) et lance un programme externe (yt-dlp) pour l'import YouTube. Considère ces entrées comme potentiellement hostiles.

## Signaler une vulnérabilité

Ne publie ni exploit, ni fichier piégé, ni plantage détaillé dans une issue publique.

Utilise le signalement privé de GitHub : onglet **Security › Report a vulnerability** du dépôt [liamls/roadie](https://github.com/liamls/roadie/security/advisories/new). Indique :

- la version (ou le commit) et ta version de macOS ;
- le composant concerné (analyse d'un fichier `.nam`, import de morceau, ouverture d'un projet, export…) ;
- l'impact et les étapes pour reproduire ;
- le journal de plantage, sans chemin personnel.

Le signalement reste privé le temps de corriger ; les détails ne sont publiés qu'après la correction.

## Périmètre

- Lecture de fichiers : `.nam`, IR `.wav`, MP3 / M4A / WAV / AIFF / FLAC, `project.json`, vidéos (corruption mémoire, blocage, consommation excessive).
- Import YouTube : l'URL est transmise à yt-dlp après `--`, jamais à un shell. Toute injection de commande ou écriture hors du dossier temporaire est une faille.
- Signature et distribution : binaire non signé ou signé avec une mauvaise identité, yt-dlp embarqué qui ne correspond pas à l'empreinte épinglée dans `CMakeLists.txt`.

Hors périmètre : une capture `.nam` qui sonne mal, ou le non-respect des conditions d'utilisation de YouTube (voir [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
