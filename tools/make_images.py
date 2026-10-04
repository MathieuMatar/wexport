#!/usr/bin/env python3
"""Generates the placeholder guide illustrations and the app icon.

The guide images are simple phone mock-ups (360x740, portrait) with labels,
standing in for real screenshots. Replace them with screenshots of the same
size and file name. No WhatsApp logo is drawn anywhere.

Requires Pillow (pip install pillow).
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(__file__), "..")
GUIDE = os.path.join(ROOT, "Assets", "Guide")
APP_ASSETS = os.path.join(ROOT, "src", "App", "Assets")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

W, H = 360, 740
ACCENT = (0, 120, 140)
HILITE = (255, 196, 0)


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def phone(title, rows, highlight=None, footer=None, dialog=None):
    """rows: list of (text, subtext). highlight: index of the row to circle."""
    img = Image.new("RGB", (W, H), (236, 239, 241))
    d = ImageDraw.Draw(img)
    # Phone body
    d.rounded_rectangle((8, 8, W - 8, H - 8), radius=36, fill=(30, 33, 36))
    d.rounded_rectangle((20, 40, W - 20, H - 40), radius=18, fill=(255, 255, 255))
    d.ellipse((W // 2 - 6, 18, W // 2 + 6, 30), fill=(70, 74, 78))
    # Status + app bar
    d.rectangle((20, 40, W - 20, 120), fill=ACCENT)
    d.rounded_rectangle((20, 40, W - 20, 70), radius=18, fill=ACCENT)
    d.text((36, 52), "12:00", font=font(13), fill="white")
    d.text((36, 82), "←  " + title, font=font(20, True), fill="white")
    y = 136
    for i, (text, sub) in enumerate(rows):
        h = 64 if sub else 48
        if highlight == i:
            d.rounded_rectangle((26, y - 4, W - 26, y + h - 4), radius=10, outline=HILITE, width=4)
        d.text((40, y + 6), text, font=font(17, highlight == i), fill=(20, 20, 20))
        if sub:
            d.text((40, y + 32), sub, font=font(13), fill=(100, 100, 100))
        d.line((36, y + h - 6, W - 36, y + h - 6), fill=(225, 225, 225))
        y += h
    if dialog:
        title_d, body_d, buttons, hi = dialog
        d.rounded_rectangle((20, 40, W - 20, H - 40), radius=18, fill=(150, 154, 158))
        d.rounded_rectangle((40, 230, W - 40, 560), radius=16, fill="white")
        d.text((60, 252), title_d, font=font(18, True), fill=(20, 20, 20))
        yy = 292
        for line in body_d:
            d.text((60, yy), line, font=font(14), fill=(60, 60, 60))
            yy += 26
        for j, b in enumerate(buttons):
            yy += 10
            d.rounded_rectangle((54, yy - 6, W - 54, yy + 30), radius=8,
                                outline=HILITE if j == hi else (210, 210, 210), width=4 if j == hi else 1)
            d.text((66, yy + 2), b, font=font(15, j == hi), fill=ACCENT)
            yy += 40
    if footer:
        d.rounded_rectangle((36, H - 150, W - 36, H - 70), radius=12, fill=(232, 245, 247), outline=ACCENT, width=2)
        yy = H - 140
        for line in footer:
            d.text((50, yy), line, font=font(14), fill=(20, 60, 70))
            yy += 20
    d.text((W - 118, H - 32), "placeholder", font=font(11), fill=(150, 150, 150))
    return img


def guide():
    os.makedirs(GUIDE, exist_ok=True)
    phone("Settings", [("Account", "Security, change number"), ("Privacy", None), ("Chats", "Theme, wallpapers, chat history"),
                       ("Notifications", None), ("Storage and data", None)], highlight=2) \
        .save(os.path.join(GUIDE, "key-01-settings.png"))
    phone("Chats", [("Display", None), ("Chat settings", None), ("Archived chats", None),
                    ("Chat backup", "Back up to Google Drive"), ("Transfer chats", None)], highlight=3) \
        .save(os.path.join(GUIDE, "key-02-chat-backup.png"))
    phone("Chat backup", [("Last backup", "Local: 18:56  Google: 18:56"), ("Back up", None),
                          ("End-to-end encrypted backup", "Off"), ("Google Account", None)], highlight=2,
          footer=["Then tap  Turn on"]) \
        .save(os.path.join(GUIDE, "key-03-e2e-turn-on.png"))
    phone("Encrypted backup", [("Create password", None), ("Use 64-digit encryption", "key instead")], highlight=1,
          footer=["Choose the 64-digit key,", "not a password"]) \
        .save(os.path.join(GUIDE, "key-04-use-64-digit.png"))
    key = phone("Your 64-digit key", [], footer=["Write it down or take a", "screenshot. Keep it safe!"])
    d = ImageDraw.Draw(key)
    groups = ["a1b2", "c3d4", "e5f6", "0718", "293a", "4b5c", "6d7e", "8f90",
              "1a2b", "3c4d", "5e6f", "7081", "92a3", "b4c5", "d6e7", "f809"]
    for i, g in enumerate(groups):
        x = 52 + (i % 4) * 66
        y = 170 + (i // 4) * 56
        d.rounded_rectangle((x - 6, y - 6, x + 56, y + 32), radius=6, outline=(200, 200, 200))
        d.text((x, y), g, font=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 18),
               fill=(20, 20, 20))
    d.rounded_rectangle((40, 420, W - 40, 470), radius=10, outline=HILITE, width=4)
    d.text((60, 432), "16 groups of 4 characters", font=font(15, True), fill=(20, 20, 20))
    key.save(os.path.join(GUIDE, "key-05-your-key.png"))
    phone("Chat backup", [("Uploading: 12%", "Already saved on the phone ✓"), ("Back up", None),
                          ("End-to-end encrypted backup", "On")], highlight=0,
          footer=["“Uploading” means the backup", "is already on your phone.", "No need to wait."]) \
        .save(os.path.join(GUIDE, "key-06-back-up.png"))
    phone("USB", [], dialog=("Use USB for", ["File transfer / Android Auto", "USB tethering", "MIDI", "PTP",
                                             "No data transfer"], ["File transfer"], 0)) \
        .save(os.path.join(GUIDE, "usb-01-file-transfer.png"))
    phone("USB", [], dialog=("Allow access to phone data?", ["The connected device will be", "able to access data on",
                                                              "this phone."], ["Allow"], 0)) \
        .save(os.path.join(GUIDE, "usb-02-allow-access.png"))


def icon():
    os.makedirs(APP_ASSETS, exist_ok=True)
    size = 512
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((16, 16, size - 16, size - 16), radius=110, fill=(0, 104, 132, 255))
    # An archive box with a speech bubble: "kept conversations".
    d.rounded_rectangle((112, 250, 400, 420), radius=24, fill=(255, 255, 255, 255))
    d.rectangle((96, 214, 416, 262), fill=(255, 255, 255, 255))
    d.rounded_rectangle((210, 290, 302, 312), radius=11, fill=(0, 104, 132, 255))
    d.rounded_rectangle((150, 92, 362, 196), radius=44, fill=(255, 210, 90, 255))
    d.polygon([(200, 186), (232, 186), (196, 222)], fill=(255, 210, 90, 255))
    for x in (206, 256, 306):
        d.ellipse((x - 14, 130, x + 14, 158), fill=(0, 104, 132, 255))
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(APP_ASSETS, "AppIcon.png"))
    img.save(os.path.join(APP_ASSETS, "AppIcon.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (256, 256)])


if __name__ == "__main__":
    guide()
    icon()
