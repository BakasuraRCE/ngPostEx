#!/usr/bin/env python3
"""
nzb_obfuscation_check.py - Analyze obfuscation level of an NZB file.

Performs TWO levels of analysis:
1. OFFLINE: Examines NZB metadata (subjects, posters, groups, message-IDs)
2. ONLINE: Connects to NNTP server(s) and fetches article headers + yEnc body
   headers to verify what an indexer would actually see on the wire.

Uses the same config file as ngPostEx for server credentials.

Usage:
    # Offline only (no server connection)
    python3 nzb_obfuscation_check.py <file.nzb>

    # Full analysis with server verification
    python3 nzb_obfuscation_check.py <file.nzb> --conf ~/.ngPostEx

    # Custom server (no config file)
    python3 nzb_obfuscation_check.py <file.nzb> --server user:pass@host:port:ssl

    # Limit number of articles to check (default: 10)
    python3 nzb_obfuscation_check.py <file.nzb> --conf ~/.ngPostEx --samples 20
"""

import sys
import ssl
import socket
import xml.etree.ElementTree as ET
import re
import argparse
from collections import Counter
from pathlib import Path


# ============================================================
# Patterns for obfuscation detection
# ============================================================
FILENAME_EXTENSIONS = re.compile(
    r'\.(rar|r\d+|zip|7z|par2|vol\d+|nfo|mkv|mp4|avi|srt|nzb|iso|img|bin|txt)(\b|")',
    re.IGNORECASE,
)
YENC_SUBJECT_PATTERN = re.compile(r"\[\d+/\d+\]\s+.+\(\d+/\d+\)")
UUID_PATTERN = re.compile(
    r"^[0-9a-f]{32}$|^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$",
    re.IGNORECASE,
)
RANDOM_STRING_PATTERN = re.compile(r"^[0-9a-zA-Z]{20,}$")
EMAIL_PATTERN = re.compile(r"[\w.-]+@[\w.-]+\.\w+")
YENC_BEGIN_PATTERN = re.compile(r"=ybegin\s+.*?name=(.+?)(\s|$)", re.IGNORECASE)


# ============================================================
# Config file parser (reads ngPostEx/ngPost config format)
# ============================================================
def parse_config_servers(config_path):
    """Parse server sections from ngPostEx config file."""
    servers = []
    current_server = None

    with open(config_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("/"):
                continue

            if line == "[server]":
                current_server = {
                    "host": "",
                    "port": 119,
                    "ssl": False,
                    "user": "",
                    "pass": "",
                    "connections": 1,
                    "enabled": True,
                    "nzbCheck": False,
                }
                servers.append(current_server)
                continue

            if current_server is not None and "=" in line:
                parts = line.split("=", 1)
                key = parts[0].strip().lower()
                val = parts[1].strip()

                if key == "host":
                    current_server["host"] = val
                elif key == "port":
                    current_server["port"] = int(val)
                elif key == "ssl":
                    current_server["ssl"] = val.lower() in ("true", "on", "1")
                elif key == "user":
                    current_server["user"] = val
                elif key == "pass":
                    current_server["pass"] = val
                elif key == "connection":
                    current_server["connections"] = int(val)
                elif key == "enabled":
                    current_server["enabled"] = val.lower() in ("true", "on", "1")
                elif key == "nzbcheck":
                    current_server["nzbCheck"] = val.lower() in ("true", "on", "1")

    # Prefer nzbCheck servers, fallback to enabled ones
    check_servers = [s for s in servers if s["nzbCheck"] and s["enabled"]]
    if check_servers:
        return check_servers
    return [s for s in servers if s["enabled"]]


def parse_server_string(server_str):
    """Parse server from command line: user:pass@host:port:ssl"""
    server = {"host": "", "port": 563, "ssl": True, "user": "", "pass": ""}

    if "@" in server_str:
        creds, hostpart = server_str.rsplit("@", 1)
        if ":" in creds:
            server["user"], server["pass"] = creds.split(":", 1)
    else:
        hostpart = server_str

    parts = hostpart.split(":")
    server["host"] = parts[0]
    if len(parts) > 1:
        server["port"] = int(parts[1])
    if len(parts) > 2:
        server["ssl"] = parts[2].lower() != "nossl"

    return server


# ============================================================
# NNTP client (minimal, for article header/body fetching)
# ============================================================
class NNTPClient:
    def __init__(self, server):
        self.server = server
        self.sock = None
        self.file = None

    def connect(self):
        """Connect and authenticate."""
        raw_sock = socket.create_connection(
            (self.server["host"], self.server["port"]), timeout=30
        )
        if self.server.get("ssl", False):
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            self.sock = ctx.wrap_socket(raw_sock, server_hostname=self.server["host"])
        else:
            self.sock = raw_sock

        self.file = self.sock.makefile("rb")
        resp = self._read_line()

        if not resp.startswith("200") and not resp.startswith("201"):
            raise ConnectionError(f"Server rejected connection: {resp}")

        # Authenticate
        if self.server.get("user"):
            self._send(f"AUTHINFO USER {self.server['user']}")
            resp = self._read_line()
            if resp.startswith("381"):
                self._send(f"AUTHINFO PASS {self.server['pass']}")
                resp = self._read_line()
                if not resp.startswith("281"):
                    raise ConnectionError(f"Authentication failed: {resp}")

    def head(self, msgid):
        """Fetch article headers via HEAD command."""
        self._send(f"HEAD <{msgid}>")
        resp = self._read_line()
        if not resp.startswith("221"):
            return None
        return self._read_multiline()

    def body_partial(self, msgid, max_lines=5):
        """Fetch first few lines of article body (for yEnc header)."""
        self._send(f"BODY <{msgid}>")
        resp = self._read_line()
        if not resp.startswith("222"):
            return None

        lines = []
        count = 0
        while True:
            line = self._read_line()
            if line == ".":
                break
            lines.append(line)
            count += 1
            if count >= max_lines:
                # Drain remaining body
                while self._read_line() != ".":
                    pass
                break
        return lines

    def stat(self, msgid):
        """Check if article exists."""
        self._send(f"STAT <{msgid}>")
        resp = self._read_line()
        return resp.startswith("223")

    def quit(self):
        try:
            self._send("QUIT")
            self.sock.close()
        except:
            pass

    def _send(self, cmd):
        self.sock.sendall(f"{cmd}\r\n".encode())

    def _read_line(self):
        line = self.file.readline().decode("utf-8", errors="replace").rstrip("\r\n")
        return line

    def _read_multiline(self):
        lines = []
        while True:
            line = self._read_line()
            if line == ".":
                break
            lines.append(line)
        return lines


# ============================================================
# NZB parser
# ============================================================
def parse_nzb(filepath):
    """Parse NZB file and extract all relevant metadata."""
    tree = ET.parse(filepath)
    root = tree.getroot()

    ns = ""
    if root.tag.startswith("{"):
        ns = root.tag.split("}")[0] + "}"

    files = []
    meta = {}

    for meta_elem in root.findall(f"{ns}head/{ns}meta"):
        meta_type = meta_elem.get("type", "")
        meta[meta_type] = meta_elem.text or ""

    for file_elem in root.findall(f"{ns}file"):
        subject = file_elem.get("subject", "")
        poster = file_elem.get("poster", "")
        date = file_elem.get("date", "")

        groups = []
        for group in file_elem.findall(f"{ns}groups/{ns}group"):
            groups.append(group.text or "")

        segments = []
        for seg in file_elem.findall(f"{ns}segments/{ns}segment"):
            segments.append(
                {
                    "number": seg.get("number", ""),
                    "bytes": seg.get("bytes", ""),
                    "msgid": seg.text or "",
                }
            )

        files.append(
            {
                "subject": subject,
                "poster": poster,
                "date": date,
                "groups": groups,
                "segments": segments,
            }
        )

    return {"meta": meta, "files": files}


# ============================================================
# Offline analysis (NZB metadata only)
# ============================================================
def check_subjects(files):
    results = {
        "total": len(files),
        "obfuscated": 0,
        "contains_filename": 0,
        "contains_yenc_pattern": 0,
        "is_uuid": 0,
        "is_random_string": 0,
        "samples": [],
    }

    for f in files:
        subject = f["subject"]
        results["samples"].append(subject[:80])

        if FILENAME_EXTENSIONS.search(subject):
            # Check if the base name is random (obfuscated archive name like "1cMZ4xSS...rar")
            # vs a real name (like "MyMovie.2024.1080p.rar")
            name_match = re.search(r'"([^"]+)"', subject)
            if name_match:
                basename = name_match.group(1)
                stem = re.sub(
                    r"\.(rar|r\d+|par2|vol\d+\+\d+\.par2|7z|zip)$",
                    "",
                    basename,
                    flags=re.IGNORECASE,
                )
                if RANDOM_STRING_PATTERN.match(stem):
                    results["is_random_string"] += 1
                    results["obfuscated"] += 1
                else:
                    results["contains_filename"] += 1
            else:
                results["contains_filename"] += 1
        elif YENC_SUBJECT_PATTERN.match(subject):
            results["contains_yenc_pattern"] += 1
        elif UUID_PATTERN.match(subject.strip()):
            results["is_uuid"] += 1
            results["obfuscated"] += 1
        elif RANDOM_STRING_PATTERN.match(subject.strip()):
            results["is_random_string"] += 1
            results["obfuscated"] += 1
        else:
            if not any(c in subject for c in ["[", "]", "(", ")", '"', "."]):
                results["obfuscated"] += 1

    return results


def check_posters(files):
    posters = [f["poster"] for f in files]
    unique_posters = set(posters)

    results = {
        "total": len(posters),
        "unique": len(unique_posters),
        "all_same": len(unique_posters) == 1,
        "all_different": len(unique_posters) == len(posters),
        "look_random": 0,
        "look_real": 0,
        "samples": list(unique_posters)[:5],
    }

    for poster in unique_posters:
        email_match = EMAIL_PATTERN.search(poster)
        if email_match:
            local_part = email_match.group().split("@")[0]
            if RANDOM_STRING_PATTERN.match(local_part) or len(local_part) > 12:
                results["look_random"] += 1
            else:
                results["look_real"] += 1
        else:
            results["look_random"] += 1

    return results


def check_groups(files):
    all_groups = []
    groups_per_file = []
    for f in files:
        all_groups.extend(f["groups"])
        groups_per_file.append(tuple(f["groups"]))

    unique_groups = set(all_groups)
    unique_group_combos = set(groups_per_file)

    results = {
        "unique_groups": sorted(unique_groups),
        "num_unique_groups": len(unique_groups),
        "all_same_group": len(unique_group_combos) == 1,
        "spread_across_groups": len(unique_group_combos) > 1,
        "policy_detected": "UNKNOWN",
    }

    if len(unique_group_combos) == 1:
        combo = list(unique_group_combos)[0]
        if len(combo) > 1:
            results["policy_detected"] = "ALL (all files on all groups)"
        else:
            results["policy_detected"] = "SINGLE_GROUP"
    elif len(unique_group_combos) == len(files):
        results["policy_detected"] = "EACH_FILE (one group per file)"
    else:
        results["policy_detected"] = "MIXED"

    return results


def check_msgids(files):
    all_msgids = []
    for f in files:
        for seg in f["segments"]:
            all_msgids.append(seg["msgid"])

    results = {
        "total_segments": len(all_msgids),
        "unique_segments": len(set(all_msgids)),
        "samples": all_msgids[:3],
    }

    if all_msgids:
        suffixes = Counter()
        for msgid in all_msgids:
            if "@" in msgid:
                suffix = msgid.split("@")[-1]
                suffixes[suffix] += 1

        results["signatures"] = dict(suffixes.most_common(5))
        most_common_sig = suffixes.most_common(1)[0] if suffixes else ("", 0)
        results["dominant_signature"] = most_common_sig[0]
        results["signature_reveals_tool"] = most_common_sig[0].lower() in [
            "ngpost",
            "ngpostex",
            "nyuu",
            "newsup",
            "powerpost",
            "yenc",
        ]

    return results


def check_meta(meta):
    return {
        "has_password": "password" in meta,
        "password_value": meta.get("password", ""),
        "has_other_meta": bool(meta),
        "all_meta": meta,
    }


# ============================================================
# Online analysis (fetch from NNTP server)
# ============================================================
def check_server_articles(files, server, num_samples=10):
    """Connect to server and fetch article headers + yEnc body info."""
    results = {
        "connected": False,
        "articles_checked": 0,
        "header_subjects": [],
        "header_froms": [],
        "yenc_filenames": [],
        "yenc_filenames_unique_per_segment": False,
        "errors": [],
    }

    # Collect sample message-IDs (spread across files)
    sample_msgids = []
    for f in files:
        if f["segments"]:
            # Take first segment of each file
            sample_msgids.append(f["segments"][0]["msgid"])
            # Also take a middle segment if available
            if len(f["segments"]) > 2:
                mid = len(f["segments"]) // 2
                sample_msgids.append(f["segments"][mid]["msgid"])
        if len(sample_msgids) >= num_samples:
            break

    sample_msgids = sample_msgids[:num_samples]

    try:
        client = NNTPClient(server)
        client.connect()
        results["connected"] = True

        for msgid in sample_msgids:
            # Fetch headers
            headers = client.head(msgid)
            if headers:
                results["articles_checked"] += 1
                for h in headers:
                    if h.lower().startswith("subject:"):
                        results["header_subjects"].append(h[8:].strip())
                    elif h.lower().startswith("from:"):
                        results["header_froms"].append(h[5:].strip())

            # Fetch body (first few lines for yEnc header)
            body_lines = client.body_partial(msgid, max_lines=3)
            if body_lines:
                for line in body_lines:
                    match = YENC_BEGIN_PATTERN.search(line)
                    if match:
                        results["yenc_filenames"].append(match.group(1))

        client.quit()

    except Exception as e:
        results["errors"].append(str(e))

    # Analyze yEnc filenames
    if results["yenc_filenames"]:
        unique_names = set(results["yenc_filenames"])
        results["yenc_filenames_unique_per_segment"] = len(unique_names) == len(
            results["yenc_filenames"]
        )

    return results


# ============================================================
# Scoring
# ============================================================
def score_obfuscation(subjects, posters, groups, msgids, meta, server_results=None):
    score = 0
    max_score = 100
    details = []

    # Subject obfuscation (30 points)
    if subjects["total"] > 0:
        subj_ratio = subjects["obfuscated"] / subjects["total"]
        subj_score = int(subj_ratio * 30)
        score += subj_score
        if subj_ratio == 1.0:
            details.append("[PASS] NZB Subjects: fully obfuscated (UUID/random)")
        elif subj_ratio > 0:
            details.append(
                f"[PARTIAL] NZB Subjects: {subjects['obfuscated']}/{subjects['total']} obfuscated"
            )
        else:
            details.append("[FAIL] NZB Subjects: contain filenames or yEnc patterns")

    # Poster randomization (15 points)
    # If server data is available, use it as the definitive source
    if (
        server_results
        and server_results.get("connected")
        and server_results.get("header_froms")
    ):
        server_froms = server_results["header_froms"]
        unique_server_froms = set(server_froms)
        if len(unique_server_froms) == len(server_froms) and len(server_froms) > 1:
            score += 15
            details.append(
                f"[PASS] Poster: unique random email per article on server ({len(unique_server_froms)} unique)"
            )
        elif len(unique_server_froms) > 1:
            score += 10
            details.append(
                f"[GOOD] Poster: {len(unique_server_froms)} unique From on server (of {len(server_froms)} checked)"
            )
        else:
            score += 5
            details.append(
                "[PARTIAL] Poster: same From on server for all checked articles"
            )
    else:
        if posters["all_different"] and posters["look_random"] > 0:
            score += 15
            details.append("[PASS] Poster: unique random email per file (NZB)")
        elif posters["look_random"] > 0:
            score += 8
            details.append(
                "[PARTIAL] Poster: random but repeated in NZB (server not checked)"
            )
        else:
            details.append("[FAIL] Poster: fixed or looks real")

    # Group spread (10 points)
    if groups["spread_across_groups"]:
        score += 10
        details.append(f"[PASS] Groups: spread ({groups['policy_detected']})")
    elif groups["num_unique_groups"] > 1:
        score += 5
        details.append(
            f"[PARTIAL] Groups: {groups['num_unique_groups']} groups, same for all files"
        )
    else:
        score += 2
        details.append(f"[WEAK] Groups: single group")

    # Message-ID signature (10 points)
    if not msgids.get("signature_reveals_tool", False):
        score += 10
        details.append(f"[PASS] Message-ID: signature does not reveal tool")
    else:
        score += 3
        details.append(
            f"[WEAK] Message-ID: '@{msgids.get('dominant_signature', '')}' reveals posting tool"
        )

    # Metadata (5 points)
    if not meta["has_other_meta"]:
        score += 5
        details.append("[PASS] Metadata: no leaks")
    else:
        score += 3
        details.append("[INFO] Metadata: meta tags present")

    # Server verification (30 points) - only if online check was done
    if server_results and server_results["connected"]:
        articles_checked = server_results["articles_checked"]
        if articles_checked == 0:
            details.append("[SKIP] Server: no articles could be fetched")
        else:
            # Check subjects on server match NZB (obfuscated)
            server_subj_obfuscated = 0
            for s in server_results["header_subjects"]:
                if UUID_PATTERN.match(s.strip()) or RANDOM_STRING_PATTERN.match(
                    s.strip()
                ):
                    server_subj_obfuscated += 1
                elif not FILENAME_EXTENSIONS.search(s):
                    server_subj_obfuscated += 1

            if server_subj_obfuscated == len(server_results["header_subjects"]):
                score += 10
                details.append(
                    f"[PASS] Server headers: subjects obfuscated ({articles_checked} checked)"
                )
            else:
                details.append(f"[FAIL] Server headers: some subjects expose filenames")

            # Check From on server
            server_froms_unique = len(set(server_results["header_froms"]))
            if server_froms_unique > 1:
                score += 5
                details.append(
                    f"[PASS] Server headers: From varies ({server_froms_unique} unique)"
                )
            else:
                score += 2
                details.append("[WEAK] Server headers: same From for all articles")

            # Check yEnc filenames (THE KEY CHECK)
            if server_results["yenc_filenames"]:
                yenc_names = server_results["yenc_filenames"]
                has_extensions = any(FILENAME_EXTENSIONS.search(n) for n in yenc_names)
                all_unique = len(set(yenc_names)) == len(yenc_names)

                if all_unique and not has_extensions:
                    score += 15
                    details.append(
                        f"[PASS] yEnc body: filenames random + unique per segment ({len(yenc_names)} checked)"
                    )
                elif not has_extensions:
                    score += 10
                    details.append(
                        f"[GOOD] yEnc body: filenames obfuscated but same per file"
                    )
                else:
                    details.append(
                        f"[FAIL] yEnc body: filenames expose real names! (e.g., '{yenc_names[0]}')"
                    )
            else:
                details.append("[SKIP] yEnc body: could not parse yEnc headers")
    elif server_results:
        details.append(f"[ERROR] Server: {'; '.join(server_results['errors'])}")
    else:
        score += 30  # Give benefit of doubt if no server check
        details.append(
            "[SKIP] Server: no online check performed (use --conf to enable)"
        )

    return score, max_score, details


# ============================================================
# Report printer
# ============================================================
def print_report(filepath, data, server_results=None):
    subjects = check_subjects(data["files"])
    posters = check_posters(data["files"])
    groups = check_groups(data["files"])
    msgids = check_msgids(data["files"])
    meta = check_meta(data["meta"])
    score, max_score, details = score_obfuscation(
        subjects, posters, groups, msgids, meta, server_results
    )

    print("=" * 70)
    print("  NZB OBFUSCATION REPORT")
    print(f"  File: {filepath}")
    if server_results and server_results["connected"]:
        print(f"  Server: {server_results.get('_host', 'connected')}")
    print("=" * 70)
    print()

    pct = int(score / max_score * 100)
    if pct >= 80:
        grade = "EXCELLENT"
    elif pct >= 60:
        grade = "GOOD"
    elif pct >= 40:
        grade = "MODERATE"
    elif pct >= 20:
        grade = "WEAK"
    else:
        grade = "NONE"

    print(f"  OBFUSCATION SCORE: {score}/{max_score} ({pct}%) - {grade}")
    print()
    print("  BREAKDOWN:")
    for detail in details:
        print(f"    {detail}")
    print()

    # NZB Subjects
    print("-" * 70)
    print("  NZB SUBJECTS")
    print(f"    Total files: {subjects['total']}")
    print(f"    UUID: {subjects['is_uuid']}, Random: {subjects['is_random_string']}")
    print(
        f"    Real filename: {subjects['contains_filename']}, yEnc pattern: {subjects['contains_yenc_pattern']}"
    )
    if subjects["samples"]:
        print("    Samples:")
        for s in subjects["samples"][:5]:
            print(f"      - {s}")
    print()

    # Posters
    print("-" * 70)
    print("  POSTER (From)")
    print(f"    NZB - Unique: {posters['unique']}/{posters['total']}")
    print(
        f"    NZB - Random: {posters['look_random']}, Real-looking: {posters['look_real']}"
    )
    if posters["samples"]:
        print("    NZB samples:")
        for s in posters["samples"][:3]:
            print(f"      - {s}")
    if (
        server_results
        and server_results.get("connected")
        and server_results.get("header_froms")
    ):
        froms = server_results["header_froms"]
        unique_froms = set(froms)
        print(f"    Server - Unique: {len(unique_froms)}/{len(froms)} articles checked")
        print("    Server samples:")
        for s in list(unique_froms)[:5]:
            print(f"      - {s}")
    print()

    # Groups
    print("-" * 70)
    print("  GROUPS")
    print(f"    Policy: {groups['policy_detected']}")
    print(f"    Groups: {', '.join(groups['unique_groups'][:5])}")
    print()

    # Message-IDs
    print("-" * 70)
    print("  MESSAGE-IDs")
    print(f"    Total segments: {msgids['total_segments']}")
    if msgids.get("signatures"):
        print("    Signatures:")
        for sig, count in msgids["signatures"].items():
            print(f"      @{sig}: {count} articles")
    print()

    # Server results
    if server_results and server_results["connected"]:
        print("-" * 70)
        print("  SERVER VERIFICATION (actual article content)")
        print(f"    Articles fetched: {server_results['articles_checked']}")

        if server_results["header_subjects"]:
            print("    Header subjects (on server):")
            for s in server_results["header_subjects"][:5]:
                print(f"      - {s[:70]}")

        if server_results["yenc_filenames"]:
            print("    yEnc filenames (in body):")
            for n in server_results["yenc_filenames"][:5]:
                print(f"      - {n}")
            unique = len(set(server_results["yenc_filenames"]))
            total = len(server_results["yenc_filenames"])
            print(f"    Unique yEnc names: {unique}/{total}", end="")
            if unique == total:
                print(" (random per segment - BEST)")
            elif unique < total:
                print(" (same per file - OK)")
            print()

        if server_results["errors"]:
            print("    Errors:")
            for e in server_results["errors"]:
                print(f"      - {e}")
        print()

    # Recommendations
    print("-" * 70)
    print("  RECOMMENDATIONS:")
    if subjects["contains_filename"] > 0:
        print("    [!] Enable article obfuscation (-x) to hide filenames from subjects")
    if posters["all_same"] and posters["look_real"] > 0:
        print("    [!] Use GEN_FROM=true for random poster emails")
    if not groups["spread_across_groups"]:
        print("    [~] Consider GROUP_POLICY=EACH_FILE for better distribution")
    if msgids.get("signature_reveals_tool"):
        print("    [~] Change msg_id to something non-identifiable")
    if server_results and server_results["yenc_filenames"]:
        has_ext = any(
            FILENAME_EXTENSIONS.search(n) for n in server_results["yenc_filenames"]
        )
        if has_ext:
            print(
                "    [!] yEnc filenames expose real names! Upgrade to ngPostEx with obfuscation enabled."
            )
        elif not server_results["yenc_filenames_unique_per_segment"]:
            print(
                "    [~] yEnc filenames are constant per file. ngPostEx randomizes per segment for better obfuscation."
            )
    if not server_results or not server_results.get("connected"):
        print("    [~] Run with --conf to verify actual server content (yEnc headers)")
    if pct >= 80:
        print("    [OK] Obfuscation looks solid.")
    print()
    print("=" * 70)


# ============================================================
# Main
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description="Analyze NZB obfuscation level (offline + online)"
    )
    parser.add_argument("nzb", help="Path to NZB file")
    parser.add_argument(
        "--conf", help="Path to ngPostEx config file (for server credentials)"
    )
    parser.add_argument("--server", help="Server string: user:pass@host:port:ssl")
    parser.add_argument(
        "--samples",
        type=int,
        default=10,
        help="Number of articles to check on server (default: 10)",
    )
    args = parser.parse_args()

    if not Path(args.nzb).exists():
        print(f"Error: file not found: {args.nzb}")
        sys.exit(1)

    try:
        data = parse_nzb(args.nzb)
    except ET.ParseError as e:
        print(f"Error parsing NZB: {e}")
        sys.exit(1)

    if not data["files"]:
        print("Error: no files found in NZB")
        sys.exit(1)

    # Determine server for online check
    server_results = None
    server = None

    if args.server:
        server = parse_server_string(args.server)
    elif args.conf:
        conf_path = Path(args.conf).expanduser()
        if conf_path.exists():
            servers = parse_config_servers(str(conf_path))
            if servers:
                server = servers[0]
            else:
                print("Warning: no enabled servers found in config")
        else:
            print(f"Warning: config file not found: {conf_path}")

    if server:
        print(
            f"Connecting to {server['host']}:{server['port']} ({'SSL' if server.get('ssl') else 'plain'})..."
        )
        server_results = check_server_articles(data["files"], server, args.samples)
        server_results["_host"] = f"{server['host']}:{server['port']}"
        if server_results["connected"]:
            print(f"Connected. Checked {server_results['articles_checked']} articles.")
        else:
            print(f"Connection failed: {'; '.join(server_results['errors'])}")
        print()

    print_report(args.nzb, data, server_results)


if __name__ == "__main__":
    main()
