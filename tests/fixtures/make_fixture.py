#!/usr/bin/env python3
"""Builds a synthetic phone-shaped WhatsApp folder with a real crypt15 backup.

The databases follow the modern Android schema closely enough for
wtsexporter 0.13.0 to process them, and are encrypted exactly the way
wtsexporter's android_crypt._decrypt_crypt15 expects (AES-256-GCM over a
zlib-compressed SQLite file, key derived from the 64-hex-digit key).

Usage: make_fixture.py <out_dir> [--messages N]
Writes:
  <out_dir>/WhatsApp/Databases/msgstore.db.crypt15     (newest backup)
  <out_dir>/WhatsApp/Databases/msgstore-2024-01-01.1.db.crypt14  (junk, must be ignored)
  <out_dir>/WhatsApp/Backups/wa.db.crypt15
  <out_dir>/WhatsApp/Media/...                          (a few real files)
  <out_dir>/contacts.vcf
  <out_dir>/key.txt                                     (the test key, hex)
  <out_dir>/plain/msgstore.db, plain/wa.db              (unencrypted copies)

Requires pycryptodome (pip install pycryptodome).
"""
import argparse
import hashlib
import hmac
import os
import sqlite3
import struct
import zlib

from Crypto.Cipher import AES

TEST_KEY = "0123456789abcdef" * 4  # 64 hex digits; never a real key

# 1x1 PNG
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d4948445200000001000000010806000000"
    "1f15c4890000000d49444154789c6360f8cfc0f01f0005000201e5274f"
    "c10000000049454e44ae426082")

SELF_USER = "96170000001"
NOW = 1_790_000_000  # seconds; the DB stores milliseconds


def derive_main_key(hex_key: str) -> bytes:
    key_stream = bytes.fromhex(hex_key)
    intermediate = hmac.new(b"\x00" * 32, key_stream, hashlib.sha256).digest()
    return hmac.new(intermediate, b"backup encryption\x01", hashlib.sha256).digest()


def encrypt_crypt15(plain_db: bytes, hex_key: str, contact_db: bool) -> bytes:
    main_key = derive_main_key(hex_key)
    iv = os.urandom(16)
    cipher = AES.new(main_key, AES.MODE_GCM, iv)
    ct, tag = cipher.encrypt_and_digest(zlib.compress(plain_db))
    # Message DB: iv = data[8:24], payload at data[0] + 2.
    # Contact DB: iv = data[7:23], payload at data[0] + 1.
    header = bytearray(32)
    if contact_db:
        header[0] = 31
        header[7:23] = iv
    else:
        header[0] = 30
        header[8:24] = iv
    footer = tag + hashlib.md5(ct).digest()  # real files end with tag + md5
    return bytes(header) + ct + footer


MSG_SCHEMA = """
CREATE TABLE jid (_id INTEGER PRIMARY KEY, user TEXT NOT NULL, server TEXT NOT NULL,
                  agent INTEGER, device INTEGER, type INTEGER, raw_string TEXT);
CREATE TABLE jid_map (lid_row_id INTEGER PRIMARY KEY, jid_row_id INTEGER);
CREATE TABLE chat (_id INTEGER PRIMARY KEY, jid_row_id INTEGER UNIQUE, hidden INTEGER DEFAULT 0,
                   subject TEXT, created_timestamp INTEGER);
CREATE TABLE message (_id INTEGER PRIMARY KEY, chat_row_id INTEGER, from_me INTEGER, key_id TEXT,
                      sender_jid_row_id INTEGER, status INTEGER, broadcast INTEGER DEFAULT 0,
                      recipient_count INTEGER, timestamp INTEGER, received_timestamp INTEGER,
                      message_type INTEGER, text_data TEXT);
CREATE TABLE message_media (message_row_id INTEGER PRIMARY KEY, chat_row_id INTEGER, file_path TEXT,
                            file_size INTEGER, message_url TEXT, mime_type TEXT, media_key BLOB,
                            file_hash TEXT, media_name TEXT, raw_transcription_text TEXT);
CREATE TABLE message_quoted (message_row_id INTEGER PRIMARY KEY, chat_row_id INTEGER, key_id TEXT,
                             text_data TEXT);
CREATE TABLE message_location (message_row_id INTEGER PRIMARY KEY, latitude REAL, longitude REAL);
CREATE TABLE message_thumbnail (message_row_id INTEGER PRIMARY KEY, thumbnail BLOB);
CREATE TABLE message_future (message_row_id INTEGER PRIMARY KEY, version INTEGER);
CREATE TABLE missed_call_logs (_id INTEGER PRIMARY KEY, message_row_id INTEGER, video_call INTEGER);
CREATE TABLE message_system (message_row_id INTEGER PRIMARY KEY, action_type INTEGER);
CREATE TABLE message_system_group (message_row_id INTEGER PRIMARY KEY, is_me_joined INTEGER);
CREATE TABLE message_system_number_change (message_row_id INTEGER PRIMARY KEY,
                                           old_jid_row_id INTEGER, new_jid_row_id INTEGER);
CREATE TABLE receipt_user (_id INTEGER PRIMARY KEY, message_row_id INTEGER, receipt_user_jid_row_id INTEGER,
                           receipt_timestamp INTEGER, read_timestamp INTEGER, played_timestamp INTEGER);
CREATE TABLE media_hash_thumbnail (media_hash TEXT PRIMARY KEY, thumbnail BLOB);
CREATE TABLE message_vcard (_id INTEGER PRIMARY KEY, message_row_id INTEGER, vcard TEXT);
CREATE TABLE message_add_on (_id INTEGER PRIMARY KEY, chat_row_id INTEGER, from_me INTEGER,
                             key_id TEXT, sender_jid_row_id INTEGER, parent_message_row_id INTEGER,
                             timestamp INTEGER);
CREATE TABLE message_add_on_reaction (message_add_on_row_id INTEGER PRIMARY KEY, reaction TEXT,
                                      sender_timestamp INTEGER);
CREATE TABLE call_log (_id INTEGER PRIMARY KEY, jid_row_id INTEGER, from_me INTEGER, call_id TEXT,
                       transaction_id INTEGER, timestamp INTEGER, video_call INTEGER, duration INTEGER,
                       call_result INTEGER, bytes_transferred INTEGER);
CREATE TABLE group_participant_user (_id INTEGER PRIMARY KEY, group_jid_row_id INTEGER,
                                     user_jid_row_id INTEGER, rank INTEGER, pending INTEGER);
"""


def build_msgstore(path: str, extra_messages: int) -> None:
    db = sqlite3.connect(path)
    db.executescript(MSG_SCHEMA)
    jids = {}

    def jid(user, server, jtype=0):
        raw = f"{user}@{server}" if user else server
        cur = db.execute("INSERT INTO jid(user, server, agent, device, type, raw_string) VALUES (?,?,0,0,?,?)",
                         (user, server, jtype, raw))
        jids[raw] = cur.lastrowid
        return cur.lastrowid

    me = jid("", "s.whatsapp.net")
    alice = jid("96171111111", "s.whatsapp.net")
    bob = jid("96172222222", "s.whatsapp.net")
    carol = jid("96173333333", "s.whatsapp.net")
    group = jid("120363023708369742", "g.us", 1)
    lid_mapped = jid("88888888888888", "lid", 17)
    lid_hidden = jid("99999999999999", "lid", 17)
    db.execute("INSERT INTO jid_map VALUES (?, ?)", (lid_mapped, carol))

    chat_alice = db.execute("INSERT INTO chat(jid_row_id, subject) VALUES (?, NULL)", (alice,)).lastrowid
    chat_bob = db.execute("INSERT INTO chat(jid_row_id, subject) VALUES (?, NULL)", (bob,)).lastrowid
    chat_group = db.execute("INSERT INTO chat(jid_row_id, subject) VALUES (?, ?)", (group, "Family   group")).lastrowid

    t = [NOW * 1000]

    def msg(chat, from_me, text=None, mtype=0, sender=0, status=13, key=None):
        t[0] += 60_000
        key = key or f"K{t[0]}"
        return db.execute(
            "INSERT INTO message(chat_row_id, from_me, key_id, sender_jid_row_id, status, timestamp,"
            " received_timestamp, message_type, text_data) VALUES (?,?,?,?,?,?,?,?,?)",
            (chat, from_me, key, sender, status, t[0], t[0], mtype, text)).lastrowid

    def media(m, chat, rel, mime):
        db.execute("INSERT INTO message_media(message_row_id, chat_row_id, file_path, mime_type, file_hash)"
                   " VALUES (?,?,?,?,?)", (m, chat, rel, mime, "aGFzaA=="))

    first = msg(chat_alice, 0, "Hello from Alice \U0001F44B", key="FIRST")
    msg(chat_alice, 1, "Hi Alice!\nSecond line")
    reply = msg(chat_alice, 0, "Replying")
    db.execute("INSERT INTO message_quoted VALUES (?,?,?,?)", (reply, chat_alice, "FIRST", "Hello from Alice"))
    img = msg(chat_alice, 0, "A caption", mtype=1)
    media(img, chat_alice, "Media/WhatsApp Images/IMG-20260101-WA0001.jpg", "image/jpeg")
    voice = msg(chat_alice, 1, None, mtype=2)
    media(voice, chat_alice, "Media/WhatsApp Voice Notes/202601/PTT-20260101-WA0002.opus", "audio/ogg; codecs=opus")
    sent = msg(chat_alice, 1, None, mtype=1)
    media(sent, chat_alice, "Media/WhatsApp Images/Sent/IMG-20260101-WA0003.jpg", "image/jpeg")
    gone = msg(chat_alice, 0, None, mtype=3)
    media(gone, chat_alice, "Media/WhatsApp Video/VID-20250101-WA0009.mp4", "video/mp4")
    doc = msg(chat_alice, 0, None, mtype=9)
    media(doc, chat_alice, "Media/WhatsApp Documents/Report été.pdf", "application/pdf")
    add_on = db.execute("INSERT INTO message_add_on(chat_row_id, from_me, sender_jid_row_id, parent_message_row_id)"
                        " VALUES (?,?,?,?)", (chat_alice, 1, 0, first)).lastrowid
    db.execute("INSERT INTO message_add_on_reaction VALUES (?,?,?)", (add_on, "❤️", t[0]))

    msg(chat_bob, 0, "Bob says hi   paragraph")
    for i in range(extra_messages):
        msg(chat_bob, i % 2, f"Bulk message {i}")

    sys_msg = msg(chat_group, 1, None, mtype=7, status=6)
    db.execute("INSERT INTO message_system VALUES (?, ?)", (sys_msg, 1))
    msg(chat_group, 0, "Group hello", sender=alice)
    msg(chat_group, 0, "From a privacy id", sender=lid_mapped)
    sticker = msg(chat_group, 0, None, mtype=20, sender=bob)
    media(sticker, chat_group, "Media/WhatsApp Stickers/STK-20260101-WA0004.webp", "image/webp")

    # Members: creator = Alice, admin = Bob, member = Carol via lid, hidden lid, and me (user '').
    for user, rank in ((alice, 2), (bob, 1), (lid_mapped, 0), (lid_hidden, 0), (me, 0)):
        db.execute("INSERT INTO group_participant_user(group_jid_row_id, user_jid_row_id, rank, pending)"
                   " VALUES (?,?,?,0)", (group, user, rank))

    db.execute("INSERT INTO call_log(jid_row_id, from_me, call_id, timestamp, video_call, duration,"
               " call_result, bytes_transferred) VALUES (?,1,'C1',?,0,125,5,1200000)", (alice, t[0]))
    db.execute("INSERT INTO call_log(jid_row_id, from_me, call_id, timestamp, video_call, duration,"
               " call_result, bytes_transferred) VALUES (?,0,'C2',?,1,0,2,0)", (bob, t[0] + 1000))
    db.commit()
    db.close()


def build_wa(path: str) -> None:
    db = sqlite3.connect(path)
    db.executescript("""
CREATE TABLE wa_contacts (_id INTEGER PRIMARY KEY, jid TEXT, is_whatsapp_user INTEGER,
                          status TEXT, display_name TEXT, wa_name TEXT);
""")
    db.execute("INSERT INTO wa_contacts(jid, is_whatsapp_user, display_name, wa_name) VALUES (?,?,?,?)",
               ("96171111111@s.whatsapp.net", 1, "Alice Contact", "alice"))
    db.execute("INSERT INTO wa_contacts(jid, is_whatsapp_user, display_name, wa_name) VALUES (?,?,?,?)",
               ("96172222222@s.whatsapp.net", 1, None, "Bob WA"))
    db.commit()
    db.close()


VCF = """BEGIN:VCARD
VERSION:2.1
N;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:;P=C3=A8re Carol;;;
FN;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:P=C3=A8re Carol
TEL;CELL:73 333 333
END:VCARD
BEGIN:VCARD
VERSION:3.0
FN:Alice From VCF
TEL;TYPE=CELL:+961 71 111 111
END:VCARD
"""


def write(path: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--messages", type=int, default=50)
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    plain = os.path.join(out, "plain")
    os.makedirs(plain, exist_ok=True)
    for name in ("msgstore.db", "wa.db"):
        p = os.path.join(plain, name)
        if os.path.exists(p):
            os.remove(p)
    build_msgstore(os.path.join(plain, "msgstore.db"), args.messages)
    build_wa(os.path.join(plain, "wa.db"))

    wa_root = os.path.join(out, "WhatsApp")
    with open(os.path.join(plain, "msgstore.db"), "rb") as f:
        write(os.path.join(wa_root, "Databases", "msgstore.db.crypt15"), encrypt_crypt15(f.read(), TEST_KEY, False))
    with open(os.path.join(plain, "wa.db"), "rb") as f:
        write(os.path.join(wa_root, "Backups", "wa.db.crypt15"), encrypt_crypt15(f.read(), TEST_KEY, True))
    write(os.path.join(wa_root, "Databases", "msgstore-2024-01-01.1.db.crypt14"), b"\0" * 256)
    write(os.path.join(wa_root, "Backups", "stickers.db.crypt15"), b"\0" * 256)

    media = os.path.join(wa_root, "Media")
    write(os.path.join(media, "WhatsApp Images", "IMG-20260101-WA0001.jpg"), PNG)
    write(os.path.join(media, "WhatsApp Images", "Sent", "IMG-20260101-WA0003.jpg"), PNG)
    write(os.path.join(media, "WhatsApp Voice Notes", "202601", "PTT-20260101-WA0002.opus"), b"OggS" + b"\0" * 60)
    write(os.path.join(media, "WhatsApp Documents", "Report été.pdf"), b"%PDF-1.4\n%%EOF\n")
    write(os.path.join(media, "WhatsApp Stickers", "STK-20260101-WA0004.webp"), b"RIFF\0\0\0\0WEBP")
    write(os.path.join(media, ".Statuses", "status.jpg"), PNG)       # hidden: skipped
    write(os.path.join(media, "WhatsApp Images", "Private", ".nomedia"), b"")
    write(os.path.join(wa_root, "accounts", "x.txt"), b"not copied")

    with open(os.path.join(out, "contacts.vcf"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(VCF)
    with open(os.path.join(out, "key.txt"), "w") as f:
        f.write(TEST_KEY)
    print(out)


if __name__ == "__main__":
    main()
