#!/usr/bin/env python3
"""End-to-end test of the whole pipeline from a phone-shaped folder.

  run_e2e.py --cli <chatkeeper-cli> --exporter <wtsexporter[.exe]> [--viewer-check]

Builds the synthetic fixture (tests/fixtures/make_fixture.py), then:
  1. a wrong key fails cleanly and keeps the copied data;
  2. the right key (same export folder, so the copy resumes) produces the
     final layout of SPEC section 7, with nothing extra left behind;
  3. optionally, the viewer opens it (tests/viewer/check-viewer.mjs).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


def run(cmd, **kw):
    print("$", " ".join(cmd), flush=True)
    return subprocess.run(cmd, text=True, capture_output=True, encoding="utf-8", errors="replace", **kw)


def check(cond, msg):
    if not cond:
        print("FAIL:", msg)
        sys.exit(1)
    print("ok:", msg)


def tree(d):
    out = set()
    for base, dirs, files in os.walk(d):
        for f in files:
            out.add(os.path.relpath(os.path.join(base, f), d).replace(os.sep, "/"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--exporter", required=True)
    ap.add_argument("--viewer-check", action="store_true")
    ap.add_argument("--work")
    a = ap.parse_args()

    work = a.work or tempfile.mkdtemp(prefix="ck-e2e-")
    fx = os.path.join(work, "fixture")
    shutil.rmtree(fx, ignore_errors=True)
    r = run([sys.executable, os.path.join(ROOT, "tests", "fixtures", "make_fixture.py"), fx, "--messages", "2000"])
    check(r.returncode == 0, "fixture built " + r.stderr[-300:])
    export = os.path.join(work, "WhatsApp Export Test Phone 2026-10-04 23-31")
    shutil.rmtree(export, ignore_errors=True)
    wrong = os.path.join(work, "wrong.txt")
    with open(wrong, "w") as f:
        f.write("f" * 64)
    key = open(os.path.join(fx, "key.txt")).read().strip()
    base = [a.cli, "--from", fx, "--exporter", a.exporter, "--viewer", os.path.join(ROOT, "viewer", "index.html"),
            "--export-dir", export, "--vcf", os.path.join(fx, "contacts.vcf"), "--country-code", "961"]

    # 1. Wrong key
    r = run(base + ["--key-file", wrong])
    print(r.stdout[-2000:], r.stderr[-2000:])
    check(r.returncode == 10, "wrong key reported as wrong key (exit 10)")
    files = tree(export)
    check("_work/msgstore.db.crypt15" in files, "copied backup kept after a wrong key")
    check("WhatsApp/Media/WhatsApp Images/IMG-20260101-WA0001.jpg" in files, "copied media kept after a wrong key")
    check("chats.js" not in files and "_work/chats.json" not in files, "no partial outputs after a wrong key")
    log = open(os.path.join(export, "export-log.txt"), encoding="utf-8").read()
    check("f" * 64 not in log and key not in log, "no key in the log")

    # 2. Right key, same folder (copy resumes)
    r = run(base + ["--key-file", os.path.join(fx, "key.txt")])
    print(r.stdout[-3000:], r.stderr[-3000:])
    check(r.returncode == 0, "export succeeded")
    check("Copied 0, skipped 7" in r.stdout, "second run skipped the files already copied")
    files = tree(export)
    expected = {
        "index.html", "chats.js", "members.js", "README.txt", "export-log.txt", "data/msgstore.db", "data/wa.db",
        "media/WhatsApp Images/IMG-20260101-WA0001.jpg", "media/WhatsApp Images/Sent/IMG-20260101-WA0003.jpg",
        "media/WhatsApp Voice Notes/202601/PTT-20260101-WA0002.opus", "media/WhatsApp Documents/Report été.pdf",
        "media/WhatsApp Stickers/STK-20260101-WA0004.webp",
    }
    check(files == expected, "final layout matches the spec: extra=%s missing=%s" % (sorted(files - expected), sorted(expected - files)))
    with open(os.path.join(ROOT, "viewer", "index.html"), "rb") as f1, open(os.path.join(export, "index.html"), "rb") as f2:
        check(f1.read() == f2.read(), "index.html copied unchanged")
    js = open(os.path.join(export, "chats.js"), encoding="utf-8").read()
    check(js.startswith("window.CHATS_JSON = ") and js.endswith(";\n"), "chats.js wrapper")
    data = json.loads(js[len("window.CHATS_JSON = "):-2])
    check(len(data["96172222222@s.whatsapp.net"]["messages"]) == 2001, "all messages present")
    img = data["96171111111@s.whatsapp.net"]["messages"]["4"]["data"].replace("\\", "/")
    check("WhatsApp/Media/WhatsApp Images/IMG-20260101-WA0001.jpg" in img, "media path resolvable by the viewer")
    members = open(os.path.join(export, "members.js"), encoding="utf-8").read()
    check(members.startswith("window.GROUP_MEMBERS = ") and '"Alice From VCF"' in members, "members.js built with vcf names")
    log = open(os.path.join(export, "export-log.txt"), encoding="utf-8").read()
    check(key not in log, "no key in the log after success")
    for line in r.stdout.splitlines():
        if line.startswith("chats="):
            print("summary:", line)
            check("chats=3" in line and "groups=1" in line and "calls=2" in line and "missing=1" in line, "summary counts")

    # 3. Viewer
    if a.viewer_check:
        env = dict(os.environ)
        r = run(["node", os.path.join(ROOT, "tests", "viewer", "check-viewer.mjs"), export], env=env)
        print(r.stdout, r.stderr)
        check(r.returncode == 0, "viewer loads the export")
    print("E2E PASSED")


if __name__ == "__main__":
    main()
