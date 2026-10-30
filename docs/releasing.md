# Publier une version

La CI vérifie chaque PR ; une release se déclenche en poussant un tag. Rien n'est signé ni notarisé sans certificat : la release le dit alors dans ses notes.

## En résumé

1. Sur `main`, CI verte. Mettre à jour `CHANGELOG.md` (renommer « Non publié » en la version) et la version dans `CMakeLists.txt` (`project(Roadie VERSION X.Y.Z …)`, sans suffixe).
2. Taguer et pousser :
   ```bash
   git tag v0.3.0-beta.1 && git push origin v0.3.0-beta.1
   ```
   Un tag avec un `-` (`-beta.1`, `-rc.1`) donne une **pré-release**.
3. Le workflow **Release** construit un DMG par architecture (`arm64` et `x86_64`), signe, notarise si les secrets existent, et publie la release avec les notes générées, les DMG et leurs `.sha256`.

Le workflow refuse de publier si le tag ne correspond pas à la version de `CMakeLists.txt` (le suffixe `-beta.N` est ignoré dans la comparaison).

Test à blanc sans rien publier : Actions › Release › *Run workflow*, avec un libellé de version (`0.3.0-rc.1`). Les DMG sont alors téléchargeables comme artefacts du run.

## Secrets GitHub (Settings › Secrets and variables › Actions)

| Secret | Contenu |
|---|---|
| `APPLE_CERTIFICATE` | Le certificat « Developer ID Application » exporté en `.p12`, encodé : `base64 -i certificat.p12 \| pbcopy` |
| `APPLE_CERTIFICATE_PASSWORD` | Le mot de passe choisi à l'export du `.p12` |
| `KEYCHAIN_PASSWORD` | N'importe quel mot de passe : il protège le trousseau temporaire du runner |
| `APPLE_ID` | L'identifiant Apple du compte développeur |
| `APPLE_PASSWORD` | Un [mot de passe d'app](https://support.apple.com/102654) (pas le mot de passe du compte) |
| `APPLE_TEAM_ID` | L'identifiant d'équipe (10 caractères, developer.apple.com › Membership) |

Sans `APPLE_CERTIFICATE`, les DMG sont signés en ad hoc : ils s'installent pour des tests (clic droit › Ouvrir), mais macOS les refuse par défaut sur un autre Mac.

## Faire la même chose en local

```bash
Tools/package_macos.sh        # test local : signature ad hoc, DMG dans dist/ (refusé par Gatekeeper sur un autre Mac)
ROADIE_SIGN_IDENTITY="Developer ID Application: Nom (TEAMID)" ROADIE_NOTARY_PROFILE=roadie-notary Tools/package_macos.sh
```

Prérequis de la version signée : certificat « Developer ID Application » dans le trousseau, et un profil de notarisation créé une fois par `xcrun notarytool store-credentials roadie-notary --apple-id … --team-id … --password <mot de passe d'app>`. Le script compile en Release dans `build-release/` (`ROADIE_BUILD_DIR` pour en réutiliser un autre), signe yt-dlp puis l'app (runtime renforcé), lance les tests, notarise et agrafe l'app puis le DMG, et écrit `dist/Roadie-<version>-<arch>.dmg` avec son `.sha256`. La version vient de `project(Roadie VERSION …)` dans `CMakeLists.txt`.

yt-dlp est **épinglé** (version et SHA-256 dans `CMakeLists.txt`) : pour le mettre à jour, changer les deux avec la ligne `yt-dlp_macos` de `SHA2-256SUMS` de la release, puis supprimer `libs/yt-dlp/yt-dlp`. Il a ses propres entitlements (`Tools/macos/yt-dlp.entitlements`) parce que c'est un exécutable PyInstaller. Licences des composants et réserves : `THIRD_PARTY_NOTICES.md`.
