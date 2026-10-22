#!/usr/bin/env python3
"""Scan the repository for credentials before publishing.

Checks .ino, .h, .cpp, .gs, .js, .html, .json, .txt files and the
contents of .aia projects (designer .scm and blocks .bky files).

Usage: python tools/check_secrets.py [repo_folder]
"""
import os, re, sys, zipfile

PATTERNS = [
    (r'(ssid|password|passwd|pass|pwd)\s*(\[\])?\s*=\s*"[^"]+"', "Wi-Fi / password"),
    (r'(api_?key|apikey|write_?key|read_?key|token|secret)\s*(\[\])?\s*=\s*"[^"]+"', "API key / token"),
    (r'AIza[0-9A-Za-z_\-]{35}', "Google / Firebase API key"),
    (r'[A-Z0-9]{16}', "ThingSpeak-like key (16 chars)"),
    (r'amqps?://[^\s"]+', "CloudAMQP URL"),
    (r'mqtts?://[^\s"]+:[^\s"]+@', "MQTT URL with password"),
    (r'https://[a-z0-9\-]+\.firebaseio\.com', "Firebase database URL"),
    (r'https://[a-z0-9\-]+\.firebasedatabase\.app', "Firebase database URL"),
    (r'[a-z0-9\-]+\.appspot\.com', "Firebase Storage bucket"),
    (r'script\.google\.com/macros/s/[A-Za-z0-9_\-]+', "Apps Script deployment URL"),
    (r'[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[a-z]{2,}', "E-mail address"),
]
TEXT_EXT = {".ino", ".h", ".cpp", ".gs", ".js", ".html", ".htm", ".json", ".txt", ".csv"}
AIA_EXT = {".scm", ".bky", ".properties"}
compiled = [(re.compile(p, re.I if i < 2 else 0), d) for i, (p, d) in enumerate(PATTERNS)]

def scan_text(label, text):
    found = 0
    for n, line in enumerate(text.splitlines(), 1):
        for rx, desc in compiled:
            m = rx.search(line)
            if m:
                print(f"{label}:{n}  [{desc}]  {m.group(0)[:80]}")
                found += 1
                break
    return found

def main(root):
    total = 0
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d not in (".git", "tools")]
        for f in fn:
            p = os.path.join(dp, f)
            ext = os.path.splitext(f)[1].lower()
            if ext in TEXT_EXT:
                with open(p, encoding="utf-8", errors="ignore") as fh:
                    total += scan_text(p, fh.read())
            elif ext == ".aia":
                with zipfile.ZipFile(p) as z:
                    for name in z.namelist():
                        if os.path.splitext(name)[1].lower() in AIA_EXT:
                            text = z.read(name).decode("utf-8", errors="ignore")
                            total += scan_text(f"{p} -> {name}", text)
    print(f"\n{total} possible credentials found." if total else "No credentials found.")
    return 1 if total else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
