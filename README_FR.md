<img align="left" width="80" height="80" src="https://raw.githubusercontent.com/BakasuraRCE/ngPostEx/master/src/resources/icons/ngPost.png" alt="ngPostEx">

# ngPostEx v5.2

**Un fork de [ngPost](https://github.com/mbruel/ngPost) par Matthieu Bruel**

ngPostEx est un fork de ngPost, le posteur Usenet en ligne de commande et interface graphique développé en C++/Qt.
Ce fork a pour but de corriger des bugs, améliorer la fiabilité, et ajouter de nouvelles fonctionnalités.

## Changements par rapport à ngPost

- Correction du `--check` faux positif quand les connexions sont refusées
- Correction de l'obfuscation yEnc ([#177](https://github.com/mbruel/ngPost/issues/177))
- Logique de reconnexion pour `--check` (utilise le même `retry` du config)
- Suppression de `QRegExp` obsolète (compatibilité Qt6 Windows)
- Fichier config par défaut : `~/.ngPostEx` / `ngPostEx.conf`

Voir [release_notes.txt](release_notes.txt) pour tous les détails.

## Description originale

**Posteur Usenet en ligne de commande et interface graphique** pour binaires développé en **C++11/Qt5**
Conçu pour être **le plus rapide possible** et offrir toutes les fonctionnalités principales pour **poster facilement et en toute sécurité**.

Fonctionnalités principales :
- **compression** (rar externe) et **génération des par2** avant de poster
- **file d'attente** pour préparer plusieurs posts
- **parallélisation** du packing et de l'upload
- **post automatique** par scan de dossier(s)
- **surveillance de dossier(s)** pour poster chaque nouveau fichier
- **suppression automatique** des fichiers une fois postés
- **exécution d'une commande** après chaque post
- **obfuscation complète** des entêtes d'articles
- Traduit en chinois, néerlandais, anglais, français, allemand, portugais et espagnol

## Compilation

### Dépendances :
- build-essential (compilateur C++, libstdc++, make,...)
- Qt5 ou Qt6 (librairies et headers)
- qmake
- libssl

### Compilation :
```bash
mkdir build && cd build
qmake6 ../src/ngPost.pro   # ou qmake pour Qt5
make -j$(nproc)
```

L'exécutable **ngPostEx** sera généré dans le dossier build.

## Utilisation

Même syntaxe que ngPost. Voir `ngPostEx --help` pour toutes les options.

### Syntaxe en ligne de commande :
```
Syntaxe: ngPostEx (options)* (-i <fichier ou dossier> | --auto <dossier> | --monitor <dossier>)+
  --help             : Aide : afficher la syntaxe
  -v or --version    : version de l'application
  -c or --conf       : fichier de configuration (si non fourni : $HOME/.ngPostEx)
  --disp_progress    : progression en CLI : NONE (défaut), BAR ou FILES
  -d or --debug      : informations supplémentaires
  -l or --lang       : langue de l'application
  --check            : vérifier un fichier nzb (articles disponibles sur Usenet)
  -q or --quiet      : mode silencieux

// post automatique
  --auto             : scan du dossier, post chaque fichier/dossier individuellement
  --monitor          : surveillance du dossier, post chaque nouveau fichier/dossier
  --rm_posted        : supprimer fichier/dossier une fois posté

// post rapide
  -i or --input      : fichier ou dossier à poster
  -o or --output     : chemin du fichier nzb de sortie
  -x or --obfuscate  : obfuscation des articles
  -g or --groups     : newsgroups (séparés par virgule sans espace)
  -m or --meta       : métadonnées (ex: "password=azerty42")
  -f or --from       : email du posteur (aléatoire si non fourni)

// compression et par2
  --tmp_dir          : dossier temporaire
  --rar_path         : chemin RAR/7z
  --rar_size         : taille des volumes RAR en Mo (0 = pas de split)
  --par2_pct         : pourcentage de redondance par2 (0 = pas de par2)
  --auto_compress    : compresser + nom/pass aléatoire + par2
  --compress         : compresser avec RAR ou 7z
  --gen_par2         : générer les par2
  --gen_name         : nom d'archive aléatoire
  --gen_pass         : mot de passe aléatoire

// serveur
  -S or --server     : serveur NNTP format compact
  -h or --host       : serveur NNTP (DNS ou IP)
  -P or --port       : port
  -s or --ssl        : utiliser SSL
  -u or --user       : utilisateur
  -p or --pass       : mot de passe
  -n or --connection : nombre de connexions
```

### Exemples :
```bash
# Surveillance d'un dossier avec compression automatique
ngPostEx --monitor /data/dossier --auto_compress --rm_posted --disp_progress files

# Post automatique avec compression et par2
ngPostEx --auto /data/dossier --compress --gen_par2 --gen_name --gen_pass --rar_size 42

# Post rapide avec compression
ngPostEx -i /tmp/fichier1 -i /tmp/dossier1 -o /nzb/monPost.nzb --compress --gen_name --gen_pass --gen_par2
```

### Vérification NZB (vérifier que les articles existent sur Usenet) :
```bash
ngPostEx --conf ~/.ngPostEx --check /chemin/vers/fichier.nzb
```

Assurez-vous qu'au moins un serveur dans votre config a `nzbCheck = true`.

### Audit d'obfuscation NZB :

Un outil Python est inclus pour analyser le niveau d'obfuscation de vos posts :
```bash
# Analyse offline (métadonnées NZB uniquement)
python3 tools/nzb_obfuscation_check.py monpost.nzb

# Analyse complète avec vérification serveur (headers + corps yEnc)
python3 tools/nzb_obfuscation_check.py monpost.nzb --conf ~/.ngPostEx
```

Donne un score (0-100%) et détaille ce qu'un indexeur pourrait déduire de votre post.

## Configuration

Le fichier de configuration par défaut est : **~/.ngPostEx** (Linux/macOS) ou **ngPostEx.conf** (Windows).
Voir [ngPost.conf](ngPost.conf) pour un exemple.

### Mots-clés disponibles uniquement dans le fichier de config :
- **POST_HISTORY** : fichier csv d'historique des posts (date, fichier, taille, vitesse, nom archive, mot de passe)
- **GROUP_POLICY** : politique de groupes (ALL, ONE_PER_POST, ONE_PER_FILE)
- **MONITOR_NZB_FOLDERS** : chaque post de monitoring va dans son propre dossier
- **NZB_POST_CMD** : commande exécutée après chaque post
- **PREPARE_PACKING** : préparer le packing du prochain post pendant l'upload
- **RAR_EXTRA** : options supplémentaires pour rar/7z
- **PAR2_PATH** / **PAR2_ARGS** : commande par2 alternative (ParPar, MultiPar)
- **TMP_RAM** : dossier temporaire en RAM (partition tmpfs)

## Crédits

ngPostEx est basé sur **ngPost** par **Matthieu Bruel** (<Matthieu.Bruel@gmail.com>).
Projet original : https://github.com/mbruel/ngPost

## Licence

**ngPostEx** est publié sous licence **GNU General Public License v3** (identique à ngPost original).

```
Copyright (C) 2020 Matthieu Bruel <Matthieu.Bruel@gmail.com>
Copyright (C) 2026 BakasuraRCE

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>
```
