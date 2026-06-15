<img align="left" width="80" height="80" src="https://raw.githubusercontent.com/BakasuraRCE/ngPostEx/master/src/resources/icons/ngPost.png" alt="ngPostEx">

# ngPostEx v5.4

**Un fork de [ngPost](https://github.com/mbruel/ngPost) par Matthieu Bruel**

ngPostEx est un fork de ngPost, le posteur Usenet en ligne de commande et interface graphique développé en C++/Qt.
Ce fork a pour but de corriger des bugs, améliorer la fiabilité, et ajouter de nouvelles fonctionnalités.

## Changements par rapport à ngPost

- Correction du `--check` faux positif quand les connexions sont refusées
- Correction de l'obfuscation yEnc ([#177](https://github.com/mbruel/ngPost/issues/177))
- Logique de reconnexion pour `--check` (utilise le même `retry` du config)
- Évaluation paresseuse avec arrêt anticipé pour `--check` (arrêt immédiat si irrécupérable)
- Analyse de récupération PAR2 avec score de santé pour `--check`
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
  --par2_block_size  : taille du bloc PAR2 en bytes pour l'analyse de récupération --check (défaut: article_size)
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

#### Analyse de récupération

Après la vérification, ngPostEx analyse la structure PAR2 du NZB et indique si le contenu est récupérable malgré les articles manquants. Le résultat inclut un score de santé conçu pour le modèle de dégradation d'Usenet (les fichiers disparaissent avec le temps à mesure que la rétention expire).

Exemple de sortie :
```
=== Recovery Analysis ===
  Data articles: 950 (missing: 12)
  PAR2 articles: 85 (missing: 3)
  PAR2 volumes: 4 intact, 1 damaged (of 5 total)
  PAR2 blocks: 63 total, 47 effective (from intact volumes only)
  PAR2 metadata: available (integrity verification possible)
  Estimated damaged blocks: 12 (block size: 716800, article size: 716800)
  Status: RECOVERABLE - 12 damaged block(s), effective recovery blocks: 47

  Health: 82/100
  >> Degraded - data intact but PAR2 redundancy reduced, re-post PAR2 volumes to restore protection
```

Les statuts possibles sont :
- **COMPLETE** — aucun article de données manquant, rien à réparer
- **RECOVERABLE** — les données manquantes peuvent être réparées avec les blocs PAR2 disponibles
- **UNRECOVERABLE** — pas assez de blocs PAR2 pour réparer les données manquantes

#### Score de santé (0-100)

Le score reflète la viabilité à long terme du NZB sur Usenet. Il privilégie la **capacité de récupération future** (blocs PAR2) par rapport à l'état actuel des données, car les articles se dégradent avec le temps à mesure que les serveurs expirent leur rétention.

**Critères de notation :**

| Critère | Poids | Description |
|---------|-------|-------------|
| Intégrité des données | 30 pts | % d'articles de données actuellement présents |
| Capacité de récupération PAR2 | 45 pts | % de blocs PAR2 encore utilisables (volumes intacts uniquement) |
| Métadonnées PAR2 | 10 pts | Au moins un fichier PAR2 intact existe (nécessaire pour la réparation) |
| Potentiel de récupération | 15 pts | Proportionnel aux blocs disponibles — mesure la capacité à réparer des dommages futurs |

**Plafond strict :** Si les données sont endommagées ET la récupération est impossible, le score est plafonné à 25 quel que soit le reste (le NZB est effectivement mort).

**Niveaux de recommandation :**

| Score | Niveau | Signification |
|-------|--------|---------------|
| ≥ 90 | Healthy | Données intactes/réparables, redondance PAR2 suffisante |
| ≥ 70 | Degraded | Redondance PAR2 réduite ; réparer bientôt ou re-poster les volumes PAR2 |
| ≥ 50 | At risk | PAR2 critique ; toute perte future de données pourrait être irrécupérable |
| ≥ 30 | Critical | Réparation à peine viable ou PAR2 presque disparu |
| < 30 | Dead | Irrécupérable, doit être re-posté depuis la source |

Les messages sont contextuels : ils différencient « données intactes mais PAR2 dégradé » (risque futur) de « données endommagées mais récupérables » (réparation nécessaire maintenant).

**Cas spéciaux :**

| Condition | Message |
|-----------|---------|
| Pas de PAR2 dans le NZB, données intactes | Critical — pas de récupération ni vérification possible |
| Pas de PAR2, données manquantes | Dead — irrécupérable |
| Tous les PAR2 endommagés (métadonnées perdues), données intactes | Critical — pas de récupération ni vérification possible |
| Tous les PAR2 endommagés, données manquantes | Dead — réparation impossible sans re-post |
| Données complètes, PAR2 partiellement endommagés | Avertissement indiquant le % de blocs PAR2 perdus (pas d'articles) |

#### Fonctionnement de l'analyse

1. Chaque volume PAR2 (`.vol*.par2`) est suivi individuellement. Si un volume perd ne serait-ce qu'un seul article, tous ses blocs de récupération sont considérés perdus (le fichier est illisible sans tous ses segments).
2. Blocs effectifs = somme des blocs des volumes dont 100% des articles sont présents.
3. Les métadonnées PAR2 sont répliquées dans chaque fichier PAR2. Tant qu'au moins un fichier PAR2 (base ou volume) est intact, les métadonnées de réparation sont disponibles.
4. Les blocs de données endommagés sont estimés à partir du nombre d'articles manquants, ajustés par le ratio entre la taille du bloc PAR2 et la taille de l'article.

#### Évaluation paresseuse avec arrêt anticipé

La vérification utilise une stratégie en deux phases pour minimiser le temps passé sur les NZB volumineux et irrécupérables :

1. **Phase 1 : Articles PAR2 en premier** — Vérifie tous les articles PAR2 pour déterminer les blocs de récupération disponibles.
2. **Phase 2 : Articles de données avec évaluation continue** — Vérifie les articles de données de manière incrémentale. Après chaque article manquant détecté, recalcule la récupérabilité. Si le NZB devient irrécupérable, toutes les connexions s'arrêtent immédiatement.

**Cas spécial :** Si le NZB ne contient aucun fichier PAR2, un seul article de données manquant déclenche l'arrêt immédiat (aucune récupération n'est possible sans PAR2).

Cela peut réduire le temps de vérification de minutes à secondes pour les NZB très endommagés (ex : un NZB de 54K articles qui prenait 156 secondes se termine maintenant en ~9 secondes quand il est irrécupérable).

#### L'option `--par2_block_size`

Par défaut, l'analyse suppose que 1 article manquant ≈ 1 bloc PAR2 endommagé. C'est correct pour par2cmdline et MultiPar qui utilisent des tailles de blocs proches de l'article (~768KB).

Si le NZB a été créé avec ParPar et des blocs larges (ex : `-s5M`), plusieurs articles perdus peuvent appartenir au même bloc PAR2 :

```bash
ngPostEx --check fichier.nzb --par2_block_size 5242880
```

Ou dans le fichier de configuration :
```
PAR2_BLOCK_SIZE = 5242880
```

#### Précision et limitations

- L'analyse est **conservatrice** (pessimiste) : elle peut indiquer UNRECOVERABLE quand la réparation serait possible, mais ne dira jamais RECOVERABLE à tort.
- Sans connaître la taille exacte du bloc PAR2 (stockée dans le fichier `.par2` lui-même), le défaut suppose le pire cas (chaque article manquant endommage un bloc différent).
- Le nombre de blocs par volume est extrait du motif `.volXX+NN.par2` où NN est le nombre de blocs de récupération. Un nommage non standard donnera 0 blocs détectés.

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
