# Project spec: WhatsApp archive app for Windows (C++ / WinUI 3)

> **For Claude Code.** This is the full brief for a Windows desktop app. Read it all before writing code. Work in the milestones at the end, and stop for review after each one. Where something here is ambiguous, or you find it doesn't work in practice, ask rather than guess.
>
> **`index.html` (supplied alongside this file) is the finished viewer.** Ship it **as is** in every export. Do not redesign it, rewrite it or generate it. It reads wtsexporter's JSON directly. The app's job is to put the right files next to it (§6–§7). If a change to `index.html` ever seems necessary, ask first.

---

## 1. What the app does

A non-technical person with an **Android phone** and a **Windows PC** runs the app. It walks them through everything and leaves them with a folder that contains their **entire WhatsApp history** (messages, photos, videos, voice notes, documents, call log and group members) and an `index.html` that shows it all in a viewer that looks and feels like WhatsApp. The folder works offline, in any browser, forever, and no longer needs the app, WhatsApp, the phone or the phone number.

Everything happens locally. **The app makes no network requests at all.**

The decryption and parsing are done by the open-source **WhatsApp Chat Exporter** (`wtsexporter`, MIT license, https://github.com/KnugiHK/WhatsApp-Chat-Exporter), shipped inside the app as a Windows executable. The app is the friendly shell around it: guidance, phone copying, running the exporter, building the viewer and cleaning up.

**App name:** use the placeholder `ChatKeeper` everywhere (one constant/resource, easy to rename). The app name, icon and artwork **must not** use the WhatsApp name or logo (trademark). Mentioning "WhatsApp" in instructional text and in the output folder name is fine.

---

## 2. Tech stack and constraints

- **Language:** C++20 with **C++/WinRT**. **UI:** WinUI 3 (Windows App SDK, latest stable 1.x).
- **Deployment:** **unpackaged and self-contained** (`WindowsPackageType=None`, `WindowsAppSDKSelfContained=true`), so users run an installer or `.exe` with no separate runtime install. Target Windows 10 1809+ and Windows 11, x64 (arm64 is optional).
- **Dependencies via vcpkg (manifest mode):** `sqlite3`, `nlohmann-json`. Add others only if clearly needed.
- **Phone access:** **Windows Portable Devices (WPD)** COM API (`PortableDeviceApi.lib`, `PortableDeviceGuids.lib`). Use `Windows.Devices.Enumeration.DeviceWatcher` for plug/unplug detection if convenient, then WPD to browse and copy.
- **Embedded viewer:** **WebView2** (built into WinUI 3) for the final preview of the supplied `index.html`.
- **Viewer:** the supplied `index.html`, embedded as an app resource (or shipped in a `viewer/` folder next to the exe) and copied unchanged into each export.
- **Bundled exporter:** the official `wtsexporter` Windows binary from its GitHub releases. Pin the version (0.13.0 at the time of writing). Check its artifact attestation as the README describes. Put it in an `exporter/` subfolder next to the app exe, with its `LICENSE`. **Verify** it includes crypt15 support (`--help` and a test decrypt). If the official binary is missing something, stop and tell me.
- **Threading:** all copying, process work and file I/O on background threads, never blocking the UI thread. Marshal UI updates with `DispatcherQueue`. Every long operation must support **Cancel**.
- **Keep the machine awake** during copy and export (`SetThreadExecutionState`).
- **Strings** in `.resw` resources, English first, ready for Arabic and French later. The layout must not break in right-to-left.
- **Visual style:** native Windows 11 look (Mica background, Fluent controls, light/dark following the system).

---

## 3. The user flow (wizard)

A single window with a step indicator (for example "1 Key · 2 Phone · 3 Copy · 4 Contacts · 5 Unlock · 6 Build · 7 Done"), Back/Next buttons, and Next disabled until the step is satisfied. The user can always go back, except while a copy or export is running; Cancel is offered there instead.

### Step 0: Welcome
- One screen: what the app does, the time and space it may take, and **"Everything stays on this computer. Nothing is uploaded."**
- Button: **Start**.
- A small secondary link: **"I already copied the WhatsApp folder to this PC"**, which skips steps 2–3 and asks for that folder. This also gives the pipeline a phone-free test path (see §11).

### Step 1: Get your key (on the phone)
An illustrated, numbered guide with one image per step (see §10 for the image list). The text, roughly:

1. Open WhatsApp → **Settings** → **Chats** → **Chat backup**.
2. Tap **End-to-end encrypted backup** → **Turn on**.
3. Choose **"Use 64-digit encryption key instead"** (not a password).
4. WhatsApp shows a 64-character key. **Write it down or take a screenshot and keep it safe.** Without it the backup can never be opened, and WhatsApp cannot recover it.
5. Tap **Back up** and wait until it says the backup is done on the phone.

Then a highlighted callout, stated clearly:

> **You don't need to wait for the upload to Google Drive to finish.** As soon as WhatsApp starts "Uploading", the backup is already saved on your phone, and that's the copy this app uses.

Also a short note: "Already have encrypted backup with a 64-digit key turned on? Just tap **Back up** to make a fresh one."

Next is labeled **"I've made the backup"**.

### Step 2: Connect your phone
- Instructions with an image: plug in with a USB cable, **unlock the phone**, and when the phone asks "Use USB for…", choose **File transfer**. If the phone asks "Allow access to phone data?", tap **Allow**.
- The app watches for devices live. States:
  - **Waiting:** a spinner and "Waiting for your phone…", plus a "Not showing up?" expander with tips (unlock the phone, choose File transfer, try another cable or port, a charge-only cable won't work).
  - **Device found but no storage visible:** "Your phone is connected but locked or not in File transfer mode. Unlock it and choose **File transfer**." This is very common, so detect it explicitly (the device enumerates but exposes no storage objects).
  - **One phone found:** shows its friendly name and manufacturer/model (for example "Galaxy J6 · Samsung") with an icon. Ask: **"Is this your phone?"** with buttons [Yes, use this phone] and [No].
  - **Several phones found:** a list to pick from. On the developer's own PC two phones were plugged in at once, so this happens.
- After confirmation, **find the WhatsApp folder** automatically (see §4.1). Show what was found:
  - "WhatsApp folder found ✓"
  - "Backup made: **today at 18:56**", read from the backup file's modified date over WPD. If the newest `msgstore.db.crypt15` is older than 24 hours, show a warning: "This backup is from 12 March. Make a fresh backup (step 1) so you get your latest messages," with [Go back to step 1] and [Continue anyway].
  - If **no `.crypt15`** exists (only `.crypt14` or older): encrypted backup with a key was never turned on. Explain this and send the user back to step 1. `.crypt14` files **cannot** be opened without root access; never try.
  - If **WhatsApp Business** is also found, ask which one to export.

### Step 3: Choose where to save, then copy
- **Location:** defaults to the user's **Desktop** (`FOLDERID_Desktop`, the real path, which may be redirected), with a [Change…] folder picker.
- Create the export folder: **`WhatsApp Export {Phone Name} {YYYY-MM-DD HH-mm}`**, for example `WhatsApp Export Galaxy J6 2026-10-04 23-31`. Strip characters Windows doesn't allow, and use no `:` in the time.
- **Before copying**, enumerate what will be copied (file count and total bytes; see §4.2) and check free space on the target drive. Required space is the total size plus 10% plus 500 MB for the decrypted database and viewer data. If there isn't enough, say how much is needed and how much is free, and offer [Change location].
- **OneDrive warning:** if the chosen folder is inside a OneDrive-synced location, warn: "This folder is synced to OneDrive. It will upload several GB of photos and videos. Choose a different folder?" Offer [Choose another] and [Continue anyway].
- **Copy screen:**
  - Overall progress bar by bytes, with "1,234 of 8,910 files · 2.1 GB of 6.4 GB · about 12 min left".
  - The current file name.
  - [Cancel].
  - Copy the small database files **first** (see §4.2), then media.
- **Resumable:** if the same export folder already contains a file with identical size, skip it. If the cable is unplugged, pause with "Phone disconnected. Reconnect it to continue", and resume automatically when it returns.
- When done: "Copied 8,910 files (6.4 GB)." Report files that failed to copy (count and list), but continue.

### Step 4: Contact names (optional)
- Explain: "WhatsApp doesn't always store your contacts' names. Add your contacts file so chats show names instead of phone numbers."
- **How to get the `.vcf` file**, in an expander with two tabs:
  - **From Google Contacts (on PC):** go to contacts.google.com, click **Export**, choose **vCard (for iOS Contacts)**, click **Export**, and the file lands in Downloads.
  - **From the phone:** open the **Contacts** app → menu → **Manage contacts** (or Settings) → **Export** → **Internal storage**. The `.vcf` file can then be found on the phone over USB; offer a button to browse the phone for it, or copy it to the PC first.
- [Choose .vcf file…] (validate that it starts with `BEGIN:VCARD`) and **[Skip]**.
- **Country code:** needed so numbers without a country code match. Pre-fill it from the Windows region (Lebanon → 961, for example) in an editable dropdown of countries with their codes.
- The `.vcf` is **read in place**, not copied into the export folder, and is never modified.

### Step 5: Enter your key
- A key entry made of **16 boxes of 4 characters**, matching how WhatsApp displays the key, with hint text.
- **Paste anywhere** fills all the boxes. Accept spaces, dashes and newlines, and upper or lower case.
- Live validation: exactly 64 hex characters (0–9, a–f). Show a ✓ when valid. The Unlock button is enabled only then.
- A [Show/Hide] toggle.
- **The key is never written to disk, logged or stored anywhere**, only held in memory for the exporter run, then cleared.

### Step 6: Unlock and build
Progress stages shown as a checklist with the current one animated:
1. **Unlocking backup**: run `wtsexporter` (§5).
2. **Reading messages**: same process; parse its progress output (§5.3).
3. **Building your archive**: write `chats.js` and `members.js`, move media into place, copy `index.html` (§6).
4. **Cleaning up** (§8).

Error handling (friendly messages, with technical details in an expander):
- **Wrong key**: "That key doesn't open this backup. Check for typos and try again." Go back to step 5 and keep the copied data. Empirically determine how `wtsexporter` reports a wrong key (exit code and/or message) by testing with a deliberately wrong key, and map it.
- Any other exporter failure: show the last lines of its output, save the full log to the export folder (no key in it), and offer [Try again].

### Step 7: Done
- A summary: number of chats, messages, photos, videos, voice notes, documents and calls; group count; "X media files were no longer on the phone" (if any); and total folder size.
- The **viewer embedded in WebView2** (map the export folder to a virtual host with `SetVirtualHostNameToFolderMapping`, for example `https://archive.local/index.html`, to avoid file:// restrictions).
- Buttons: **[Open in browser]** (opens `index.html` with the default browser), **[Open folder]**, **[Done]**.
- Advice box: "Keep a copy of this folder on another drive or in cloud storage. Also keep your 64-digit key somewhere safe. Everything in this folder already works without it, but you'll need it to unlock the original backup again."

---

## 4. Phone access details (WPD)

### 4.1 Finding the WhatsApp folder
Search each storage object on the device (internal storage is usually named "Phone" or "Internal storage"; an SD card may also be present, so check all storages), in this order:
1. `Android/media/com.whatsapp/WhatsApp`, the current location (Android 11+, and many Android 10 phones).
2. `WhatsApp`, the legacy location at the storage root.
3. WhatsApp Business: `Android/media/com.whatsapp.w4b/WhatsApp Business`, then `WhatsApp Business`.

Expected structure inside:
```
WhatsApp/
  Databases/   msgstore.db.crypt15        ← the newest backup (needed)
               msgstore-YYYY-MM-DD.1.db.crypt14   ← old, ignore
               msgstore-increment-*.crypt14       ← ignore
  Backups/     wa.db.crypt15              ← contacts database (needed if present)
               (other *.crypt15, Stickers/, Wallpapers/ – not needed)
  Media/       WhatsApp Images/, WhatsApp Video/, WhatsApp Voice Notes/, WhatsApp Audio/,
               WhatsApp Documents/, WhatsApp Stickers/, WhatsApp Animated Gifs/,
               WhatsApp Profile Photos/, … (needed – everything)
  accounts/    (not needed)
```
Note: File Explorer does not show modified dates over MTP, but WPD exposes `WPD_OBJECT_DATE_MODIFIED`. Use it for the backup-date check in step 2.

### 4.2 What to copy
- `Databases/msgstore.db.crypt15` (the newest by date if there are several `.crypt15` files) → `_work\msgstore.db.crypt15` (§7). **Copy this first.**
- `Backups/wa.db.crypt15`, if present → `_work\wa.db.crypt15`.
- The entire `Media/` tree → **`WhatsApp\Media\`** in the export folder (renamed to `media\` later, see §6), recursively, preserving the folder structure and exact file names. Include subfolders such as `Sent/` and `Private/`. Hidden folders whose names start with `.` (for example `.Statuses`, `.trash`, `.Links`) may be skipped. Make this a single constant list that's easy to change.
- Nothing else.

### 4.3 Copying robustly
- Enumerate first for an accurate total, then stream each file with `IPortableDeviceResources::GetStream` into a temp file in the destination, renaming on completion. Set the destination file's modified time to the source's.
- Expect thousands of small files. Avoid per-file overhead where possible (reuse COM objects, use reasonable buffer sizes) and measure throughput.
- Handle: the device disappearing mid-copy (pause and resume), individual file errors (log, skip, count), file names with characters Windows dislikes (sanitize consistently, and record the mapping so §6 can still resolve the media path), and very long paths (enable long-path support in the manifest, or use `\\?\` paths).
- **Never write to, rename or delete anything on the phone.** The app treats the phone as read-only.

---

## 5. Running wtsexporter

### 5.1 Command
Run with the **working directory = the export folder** (§7 layout):
```
exporter\wtsexporter.exe -a --no-banner
    -k <64-hex-key>
    -b "_work\msgstore.db.crypt15"
    --wab "_work\wa.db.crypt15"                      (only if that file was found)
    -d "data\msgstore.db"
    -w "data\wa.db"
    -m "WhatsApp"
    -o "."
    -j "_work\chats.json"
    --no-html
    --enrich-from-vcards "<path to .vcf>"             (only if provided)
    --default-country-code <code>                     (only with the .vcf)
```
Check every flag against `wtsexporter.exe --help` for the pinned version, and confirm `-d`/`-w` are where the decrypted databases get written when used with `-b`/`--wab`. If not, find where they land and move them into `data\`.

**Why `-m WhatsApp -o .` matters (a lesson learned the hard way):** after processing, wtsexporter copies the media folder into `<output>/<media>` **unless that path already exists**. With `-o .` and `-m WhatsApp` that path is the media folder itself, so it's skipped. Any other combination either duplicates gigabytes of media or, with `-c`, moves it out from under the paths recorded in the JSON. **Never pass `-c`.** Never let the exporter move or copy media.

### 5.2 Process handling
- `CreateProcessW` with stdout and stderr redirected to pipes, no console window (`CREATE_NO_WINDOW`), and environment `PYTHONUTF8=1` and `PYTHONIOENCODING=utf-8`. Its banner is known to crash on non-UTF-8 consoles, hence also `--no-banner`. Read both pipes on background threads.
- **Never pass `--check-update`**, which would make a network request. Never pass `-k` without a value: wtsexporter would then prompt for the key on a console, which would hang.
- The key appears on the exporter's command line. Accept this; the key file option expects a binary key file, not hex text. Never log the command line with the key in it; redact it.
- Cancel = terminate the process tree, then clean up partial outputs.

### 5.3 Progress
wtsexporter logs lines like `[INFO] Processing messages...(12345)` and uses `tqdm` progress bars (carriage-return updates, usually on stderr). Parse what's available to drive a determinate bar where possible, and fall back to indeterminate plus the latest status line. Capture real output from a test run and build the parser from that, not from guesses.

---

## 6. Building the archive for the viewer

The supplied `index.html` does all conversion of wtsexporter's JSON in the browser. **There is no C++ transform of messages.** The app only needs to produce these files next to it.

### 6.1 `chats.js` (required)
`index.html` loads `<script src="chats.js">` and expects:
```js
window.CHATS_JSON = <the exact contents of chats.json>;
```
- Write it by **streaming**: write the prefix `window.CHATS_JSON = `, copy `_work\chats.json` byte for byte, then write `;\n`. Don't parse and re-serialize it; it can be hundreds of MB.
- **Safety:** this is loaded as a script, so the JSON must be valid as a JavaScript expression. JSON is valid JS except for the raw characters U+2028/U+2029 inside strings, which are fine in modern browsers but escape them anyway (` `, ` `) while streaming. wtsexporter writes JSON with `ensure_ascii` by default, so they should already be escaped. Verify. A `</script>` sequence doesn't matter here because it's an external `.js` file, not inline.
- Validate before cleanup: the file exists, its size ≈ the JSON size + the prefix, and the JSON parsed successfully once (a SAX/validation pass with nlohmann/json, without building a DOM, is enough). Read the chat count and message count for the summary screen while doing that pass.
- The JSON structure, for reference: top-level keys are chat JIDs (`…@s.whatsapp.net` person, `…@g.us` group, `000000000000000` the call log). Each chat has `name`, `messages` (an object of message id → message with `from_me`, `timestamp`, `meta`, `media`, `data`, `sender`, `mime`, `caption`, `reply`, `quoted_data`, `sticker`, `reactions`, …). `index.html` handles all of it.

### 6.2 `media\` folder (required for photos, videos and voice notes)
`index.html` resolves every media path by taking the part after `WhatsApp/Media/` and loading it from **`media/`** next to `index.html` (`media/WhatsApp Images/IMG-….jpg`). If that substring isn't in the path, it falls back to `media/<file name>`.
- wtsexporter needs the files at `WhatsApp\Media\…` **while it runs**, because it checks that each media file exists and marks missing ones as `"The media is missing"`. So the order is: copy the phone's `Media` to `WhatsApp\Media\` (§4.2), run wtsexporter (§5), and **then move `WhatsApp\Media` → `media`** (a same-drive rename, instant) and remove the now-empty `WhatsApp\` folder.
- Before moving, confirm that a sample of media paths in the JSON contain `WhatsApp/Media/` after converting `\` → `/`. They will, given `-m WhatsApp`. If wtsexporter wrote absolute paths, that's fine too, since the viewer only uses the part after `WhatsApp/Media/`.
- If the move fails (for example, a file is open), retry, then report it. The viewer won't find media until `media\` exists.

### 6.3 `members.js` (optional; enables the group members panel)
`index.html` loads `<script src="members.js">` and, if present, reads:
```js
window.GROUP_MEMBERS = {
  "120363023708369742": [            // group id = the part of the group JID before "@g.us"
    { "n": "Père Hanna", "p": "9613366160", "a": 2 },   // n = name ("" if unknown), p = phone digits ("" if hidden), a = 0 member / 1 admin / 2 creator
    { "n": "", "p": "", "a": 0 }                         // hidden-number participant
  ],
  ...
};
```
Exclude the user themselves (the viewer adds "You"). Sort by admin rank descending, then named before unnamed, then by name.

Build it in C++ from **`data\msgstore.db`** (SQLite, read-only):
- **Newer schema:** `group_participant_user` (`group_jid_row_id`, `user_jid_row_id`, `rank`) joined to `jid` (`_id`, `user`, `server`). The group id is `jid.user` of the group row. A participant with server `s.whatsapp.net` has phone = `jid.user`. A participant with server `lid` is a privacy ID: map it via `jid_map` (`lid_row_id` → `jid_row_id`) to a phone if the table and row exist, else phone = "". The participant row whose `jid.user` is empty is the user themselves; skip it.
- **Older schema:** `group_participants` (`gjid`, `jid`, `admin`).
- Detect which tables and columns exist (`sqlite_master`, `PRAGMA table_info`) and degrade gracefully. If nothing works, write no `members.js` (the viewer copes; it loads the file with a plain script tag, so a missing file is harmless).
- **Names** for `n`, in order: (1) the user's `.vcf`, if given (FN + TEL; normalize numbers by stripping non-digits, dropping a `00` prefix, and replacing a leading `0`, or prepending to a number of 8 or fewer digits, with the country code; decode quoted-printable FN); (2) `data\wa.db` → `wa_contacts` (`jid` like `961…@s.whatsapp.net`, `display_name`, `wa_name`).
- Only **current** members exist in the database; people who left aren't recorded. The viewer already says so.

### 6.4 `index.html`
Copy the supplied file unchanged into the export folder.

---

## 7. Export folder layout

**During the run:**
```
WhatsApp Export Galaxy J6 2026-10-04 23-31\
  _work\msgstore.db.crypt15     ← copied from the phone
  _work\wa.db.crypt15           ← copied from the phone (if present)
  _work\chats.json              ← wtsexporter output
  WhatsApp\Media\...            ← copied from the phone (where wtsexporter expects it)
  data\msgstore.db, data\wa.db  ← decrypted by wtsexporter
```

**Final:**
```
WhatsApp Export Galaxy J6 2026-10-04 23-31\
  index.html            ← the supplied viewer, unchanged (open this)
  chats.js              ← window.CHATS_JSON = <chats.json>
  members.js            ← window.GROUP_MEMBERS = {...}   (if it could be built)
  media\...             ← all photos, videos, voice notes, documents (mirrors the phone's WhatsApp/Media)
  data\msgstore.db      ← decrypted message database (kept: full raw history, SQLite)
  data\wa.db            ← decrypted contacts database (kept, if it existed)
  export-log.txt        ← what happened (no key), useful for support
  README.txt            ← plain language: what this folder is, open index.html, keep 2 copies
```

---

## 8. Cleanup (end of a successful run)

Only after `chats.js` is written and validated (§6.1) **and** `media\` is in place, delete:
- `_work\` entirely: `msgstore.db.crypt15`, `wa.db.crypt15`, `chats.json` (its contents now live in `chats.js`), and temp files.
- The empty `WhatsApp\` folder left after moving `Media`.
- Any partial or temp files left by the copier.

**Keep:** `index.html`, `chats.js`, `members.js`, `media\`, `data\msgstore.db`, `data\wa.db`, the log and the README.
**Never touch:** anything on the phone, the user's `.vcf`, or anything outside the export folder.

On failure or cancel: keep the copied data (`_work\*.crypt15`, `WhatsApp\Media`, or `media\` if already moved) so a retry doesn't recopy, but delete partial outputs (`chats.js`, `members.js`, half-written files). Re-entering the flow with the same export folder resumes. Handle both media locations when resuming.

---

## 9. The viewer

The viewer is the supplied **`index.html`**, final and not to be modified. For reference, it already provides the chat list with search, filters and sorting; WhatsApp-style bubbles; day separators; replies with jump-to-original; reactions; inline photos, videos, voice notes (with playback speed), stickers and documents; a lightbox; a call log with parsed call types and durations; group members, media and info panels; in-chat and global search; jump to date; light/dark/auto theme; RTL handling; and paged rendering for very large chats.

What the app must guarantee for it to work:
- `chats.js`, `members.js` (optional) and `media\` sit **next to** `index.html` with exactly those names (§6–§7).
- For the **embedded WebView2 preview** (step 7), map the export folder to a virtual host (`SetVirtualHostNameToFolderMapping`, for example `https://archive.local/index.html`) so the script tags and media load. `index.html` uses `localStorage` for the theme choice, which works on the virtual host.
- The **[Open in browser]** button opens `index.html` via `file://` with the default browser. This works because the data comes from script tags, not `fetch`. (If `chats.js` were missing, the page would show a "choose chats.json" picker, which should never happen in an app-made export.)
- Test the result in Edge, Chrome and Firefox via `file://`, with a large archive (about 300 chats, about 500,000 messages, one chat with 60,000), and report load time and memory. If `chats.js` is too heavy for the browser at the largest sizes, report numbers and **ask** before changing anything.

---

## 10. Guide images (step 1 and step 2)

Claude Code can't produce real WhatsApp screenshots. Create **clean placeholder illustrations** (simple vector-style phone mockups with the relevant labels, using no WhatsApp logo) at these paths, and wire them up. I'll replace them with real screenshots later, so keep the image sizes and aspect ratio consistent (portrait phone, about 360×740) and the file names stable:
```
Assets/Guide/key-01-settings.png        (Settings → Chats)
Assets/Guide/key-02-chat-backup.png     (Chat backup screen)
Assets/Guide/key-03-e2e-turn-on.png     (End-to-end encrypted backup → Turn on)
Assets/Guide/key-04-use-64-digit.png    (“Use 64-digit encryption key instead”)
Assets/Guide/key-05-your-key.png        (the key screen: 16 groups of 4 – write it down)
Assets/Guide/key-06-back-up.png         (Back up → “Uploading” = already saved on phone)
Assets/Guide/usb-01-file-transfer.png   (USB prompt → File transfer)
Assets/Guide/usb-02-allow-access.png    (Allow access to phone data → Allow)
```

---

## 11. Code structure (suggested)

```
ChatKeeper/
  ChatKeeper.sln
  src/App/                 WinUI 3 app: App.xaml, MainWindow, one Page per wizard step, view models
  src/Core/                no WinUI here — plain C++20, unit-testable:
    Device/                IDeviceSource interface; WpdDeviceSource; FolderDeviceSource (for "I already copied it" + tests)
    Copy/                  CopyPlan (enumerate), Copier (resume, progress, cancel)
    Exporter/              ExporterRunner (process, pipes, progress parsing, error mapping)
    Archive/               ChatsJsWriter (stream + validate), MediaMover, MembersBuilder (SQLite + vcf + wa.db), Cleanup
    Util/                  paths, free space, OneDrive detection, country codes, logging (redaction)
  viewer/                  index.html (supplied, unchanged; copied into each export)
  exporter/                wtsexporter.exe + LICENSE (pinned version, checked into build pipeline, not source control if large)
  tests/                   unit tests for Core (transform fixtures, vcf parsing, number normalization, path resolution, cleanup)
  installer/               Inno Setup script (or similar) producing a single setup .exe
  THIRD_PARTY_NOTICES.txt  wtsexporter (MIT), SQLite, nlohmann/json, Windows App SDK
```
Put the device behind the `IDeviceSource` interface so the whole pipeline (copy → export → build → cleanup) runs end to end from a local folder without a phone. This is used by the "I already copied the folder" path and by automated tests.

---

## 12. Quality bar and testing

- **End-to-end test from a folder:** build a fixture folder shaped like the phone's `WhatsApp/` with a real or synthetic `msgstore.db.crypt15` and a known key. (Generate one: wtsexporter's source shows the crypt15 format. Or I'll provide a real test backup on request.) Run the full pipeline and assert the outputs.
- **Unit tests:** `chats.js` streaming (byte-exact JSON inside, U+2028/9 escaping, large files), members (new and old schema, lid mapping, missing tables, vcf parsing, number normalization), media move and resume, and cleanup (deletes only what §8 lists).
- **Viewer integration:** open the resulting export's `index.html` in a headless browser (for example via WebView2 or Playwright in a test project), check that chats load, a known photo renders from `media/`, and the members panel shows for a group.
- **Manual device test checklist:** a locked phone, a charge-only cable, two phones connected, unplugging mid-copy, a full disk, a OneDrive Desktop, a wrong key, no `.crypt15`, WhatsApp Business.
- No crashes on unexpected data. Every error a user can hit has a plain-language message and a way forward.
- No network calls (verify).

---

## 13. Milestones (stop for review after each)

1. **Skeleton:** the WinUI 3 unpackaged self-contained app builds and runs, with wizard navigation and all step pages stubbed. The vcpkg setup and the bundled `wtsexporter.exe` are verified (`--help`).
2. **Device layer:** WPD detection (waiting, locked or no storage, one or many devices), confirmation, finding the WhatsApp folder, and reading the backup date. Plus `FolderDeviceSource`.
3. **Copier:** enumeration, space check, OneDrive warning, export folder naming, a robust copy with progress, cancel, resume and disconnect handling.
4. **Exporter runner:** contacts step, key entry, running wtsexporter with correct flags, progress parsing, wrong-key detection.
5. **Archive output:** `chats.js` writer, media move to `media\`, `members.js` builder, copy `index.html`, and tests. Verify the export opens correctly in a browser.
6. **Finish:** cleanup, summary, embedded WebView2 preview, README and log, guide placeholder images, installer, third-party notices.
