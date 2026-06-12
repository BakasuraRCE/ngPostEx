<img align="left" width="80" height="80" src="https://raw.githubusercontent.com/BakasuraRCE/ngPostEx/master/src/resources/icons/ngPost.png" alt="ngPostEx">

# ngPostEx v5.3

**A fork of [ngPost](https://github.com/mbruel/ngPost) by Matthieu Bruel**

ngPostEx is a fork of ngPost, the Command Line and GUI Usenet binary poster developed in C++/Qt.
This fork aims to fix bugs, improve reliability, and add new features on top of the original codebase.

## Changes from ngPost

- Fixed `--check` false-positive when all connections are refused
- Fixed yEnc filename obfuscation ([#177](https://github.com/mbruel/ngPost/issues/177))
- Connection retry logic for `--check` (uses same `retry` config as posting)
- Removed deprecated `QRegExp` (Qt6 compatibility on Windows)
- Default config file: `~/.ngPostEx` / `ngPostEx.conf`

See [release_notes.txt](release_notes.txt) for full details.

## Original Description

**Command Line and sexy GUI Usenet poster** for binaries developed in **C++11/Qt5**
It is designed to be **as fast as possible** and offer ALL the main features **to post data easily and safely**.

Main features:
- **compress** (using external rar binary) and **generate par2** before posting
- **posting queue** to prepare several posts
- **packs the next Post while uploading the current one**
- **automate posts** by **scanning folder(s)**
- **monitor folder(s) to post each new file/folder**
- **auto delete files/folders once posted**
- **execute a COMMAND or script after each Post**
- **switch off the computer** when all posts are finished
- **full obfuscation** of Article Headers
- Translated in Chinese, Dutch, English, French, German, Portuguese and Spanish

## How to build

### Dependencies:
- build-essential (C++ compiler, libstdc++, make,...)
- Qt5 or Qt6 libraries and headers
- qmake (to generate the Makefile)
- libssl

### Build:
```bash
mkdir build && cd build
qmake6 ../src/ngPost.pro   # or qmake for Qt5
make -j$(nproc)
```

The executable **ngPostEx** will be generated in the build directory.

For command-line only (no GUI):
```bash
qmake6 ../src/ngPost_cmd.pro
make -j$(nproc)
```

## How to use

Same syntax as ngPost. See `ngPostEx --help` for full options.

### Command line syntax:
```
Syntax: ngPostEx (options)* (-i <file or folder> | --auto <folder> | --monitor <folder>)+
  --help             : Help: display syntax
  -v or --version    : app version
  -c or --conf       : use configuration file (if not provided, we try to load $HOME/.ngPostEx)
  --disp_progress    : display cmd progressbar: NONE (default), BAR or FILES
  -d or --debug      : display extra information
  --fulldebug        : display full debug information
  -l or --lang       : application language
  --check            : check nzb file (if articles are available on Usenet)
  -q or --quiet      : quiet mode (no output on stdout)

// automated posting (scanning and/or monitoring)
  --auto             : parse directory and post every file/folder separately
  --monitor          : monitor directory and post every new file/folder
  --rm_posted        : delete file/folder once posted (requires --auto or --monitor)

// quick posting (several files/folders)
  -i or --input      : input file to upload (single file or directory)
  -o or --output     : output file path (nzb)
  -x or --obfuscate  : obfuscate the subjects of the articles
  -g or --groups     : newsgroups where to post (comma separated without space)
  -m or --meta       : extra meta data in header (e.g. "password=qwerty42")
  -f or --from       : poster email (random one if not provided)
  -a or --article_size: article size (default: 716800)
  -z or --msg_id     : msg id signature, after the @ (default: ngPost)
  -r or --retry      : number of retries for failed articles (default: 5)
  -t or --thread     : number of threads
  --gen_from         : generate a new random email for each Post

// compression and par2 support
  --tmp_dir          : temporary folder for compressed files and par2
  --rar_path         : RAR/7z absolute file path
  --rar_size         : size in MB of RAR volumes (0 = no split)
  --rar_max          : maximum number of archive volumes
  --par2_pct         : par2 redundancy percentage (0 = no par2)
  --par2_path        : par2 absolute file path
  --par2_block_size  : PAR2 block size in bytes for --check recovery analysis (default: article_size)
  --auto_compress    : compress with random name/password + generate par2
  --compress         : compress inputs using RAR or 7z
  --gen_par2         : generate par2 (with --compress)
  --rar_name         : provide the RAR file name
  --rar_pass         : provide the RAR password
  --gen_name         : generate random RAR name
  --gen_pass         : generate random RAR password
  --length_name      : length of random RAR name (default: 17)
  --length_pass      : length of random RAR password (default: 13)

// server parameters
  -S or --server     : NNTP server: (<user>:<pass>@@@)?<host>:<port>:<nbCons>:(no)?ssl
  -h or --host       : NNTP server hostname (or IP)
  -P or --port       : NNTP server port
  -s or --ssl        : use SSL
  -u or --user       : NNTP server username
  -p or --pass       : NNTP server password
  -n or --connection : number of NNTP connections
```

### Examples:
```bash
# Monitor a folder, auto compress, delete after posting
ngPostEx --monitor /data/folder --auto_compress --rm_posted --disp_progress files

# Auto post a folder with compression and par2
ngPostEx --auto /data/folder --compress --gen_par2 --gen_name --gen_pass --rar_size 42

# Quick post with compression
ngPostEx -i /tmp/file1 -i /tmp/folder1 -o /nzb/myPost.nzb --compress --gen_name --gen_pass --gen_par2

# With config file
ngPostEx -c ~/.ngPostEx -m "password=qwerty42" -i /tmp/file1 -i /tmp/folder1
```

### NZB Check (verify articles exist on Usenet):
```bash
ngPostEx --conf ~/.ngPostEx --check /path/to/file.nzb
```

Make sure at least one server in your config has `nzbCheck = true`.

#### Recovery analysis

When the check completes, ngPostEx analyzes the NZB's PAR2 structure and reports whether the content is recoverable despite missing articles. The output looks like:

```
=== Recovery Analysis ===
  Data articles: 950 (missing: 12)
  PAR2 articles: 85 (missing: 3)
  PAR2 volumes: 4 intact, 1 damaged (of 5 total)
  PAR2 blocks: 63 total, 47 effective (from intact volumes only)
  Estimated damaged blocks: 12 (block size: 716800, article size: 716800)
  Status: RECOVERABLE - 12 damaged block(s), effective recovery blocks: 47
```

The possible statuses are:
- **COMPLETE** — no missing articles, nothing to repair
- **RECOVERABLE** — missing data can be repaired with available PAR2 blocks
- **UNRECOVERABLE** — not enough PAR2 blocks to repair the missing data

#### How the analysis works

1. Each PAR2 volume file (`.vol*.par2`) is tracked individually. If a volume loses even one article, its entire set of recovery blocks is considered lost (the file is unreadable without all its segments).
2. Effective recovery blocks = sum of blocks from volumes that have 100% of their articles present.
3. PAR2 metadata is stored in every PAR2 file. As long as at least one PAR2 file (base or volume) is fully intact, repair metadata is available.
4. Damaged data blocks are estimated from the number of missing data articles, adjusted by the ratio between PAR2 block size and article size.

#### The `--par2_block_size` option

By default, the analysis assumes 1 missing article ≈ 1 damaged PAR2 block (block size = article size). This is accurate for par2cmdline and MultiPar which typically use block sizes close to the article size (~768KB).

If the NZB was created with ParPar using large block sizes (e.g. `-s5M`), multiple lost articles may fall within the same PAR2 block, meaning fewer recovery blocks are needed. In this case:

```bash
ngPostEx --check file.nzb --par2_block_size 5242880
```

Or in the config file:
```
PAR2_BLOCK_SIZE = 5242880
```

#### Precision and limitations

- The analysis is **conservative** (pessimistic): it may report UNRECOVERABLE when repair is actually possible, but will never report RECOVERABLE when it isn't.
- Without knowing the exact PAR2 block size (which is stored inside the `.par2` file), the default assumes worst-case (each missing article damages a different block).
- The volume block count is extracted from the filename pattern `.volXX+NN.par2` where NN is the number of recovery blocks. Non-standard PAR2 naming will result in 0 detected blocks.

### NZB Obfuscation Audit:

A Python tool is included to analyze how well your posts are obfuscated:
```bash
# Offline analysis (NZB metadata only)
python3 tools/nzb_obfuscation_check.py mypost.nzb

# Full analysis with server verification (checks actual article headers + yEnc body)
python3 tools/nzb_obfuscation_check.py mypost.nzb --conf ~/.ngPostEx
```

Reports a score (0-100%) and details what an indexer could infer from your post.

## Configuration

The default configuration file is: **~/.ngPostEx** (Linux/macOS) or **ngPostEx.conf** (Windows).
See [ngPost.conf](ngPost.conf) for an example.

### Config-only keywords (not available as CLI arguments):
- **MONITOR_NZB_FOLDERS**: each monitoring post goes in its own folder
- **POST_HISTORY**: csv file logging all successful posts (date, file, size, speed, archive name, password)
- **GROUP_POLICY**: policy for posting with multiple groups (ALL, ONE_PER_POST, ONE_PER_FILE)
- **NZB_RM_ACCENTS**: remove accents from nzb file names
- **AUTO_CLOSE_TABS**: close GUI tabs when posted successfully
- **RESUME_WAIT**: seconds to wait before auto-resuming after network loss (min: 30)
- **NO_RESUME_AUTO**: stop a post when network is lost
- **PREPARE_PACKING**: prepare packing of next post while uploading current one
- **NZB_POST_CMD**: execute a command/script after each post
- **RAR_EXTRA**: customize rar/7z command arguments
- **PAR2_PATH**: path to alternative par2 generator (ParPar, MultiPar)
- **PAR2_ARGS**: customize par2 command arguments
- **TMP_RAM**: temporary folder with size constraint (e.g. tmpfs partition)
- **TMP_RAM_RATIO**: ratio for par2 size estimation on TMP_RAM

## Credits

ngPostEx is based on **ngPost** by **Matthieu Bruel** (<Matthieu.Bruel@gmail.com>).
Original project: https://github.com/mbruel/ngPost

### Original contributors:
- Uukrull for intensive testing and MacOS packages
- awsms for 10Gb/s testing
- animetosho for ParPar
- demanuel for NewsUP
- noobcoder1983, tensai, yuppie for German translation
- tiriclote for Spanish translation
- hunesco for Portuguese translation
- Peng for Chinese translation

## License

**ngPostEx** is published under the **GNU General Public License v3** (same as the original ngPost).

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
