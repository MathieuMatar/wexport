# ChatKeeper (placeholder name)

A Windows app (C++20, C++/WinRT, WinUI 3) that walks a non-technical person
through turning their Android phone's WhatsApp into a folder on their PC: every
message, photo, video, voice note, document, call and group member, readable
offline in any browser through the supplied `viewer/index.html`.
Everything happens locally. See [SPEC.md](SPEC.md) for the full brief.

Decryption and parsing are done by the bundled official
[WhatsApp Chat Exporter](https://github.com/KnugiHK/WhatsApp-Chat-Exporter)
(`wtsexporter` 0.13.0, MIT). The app is the shell around it.

## Layout

```
src/Core/      plain C++20, no WinUI, unit-tested (also builds on Linux)
  Device/      IDeviceSource, WpdDeviceSource (phone over MTP), FolderDeviceSource, WhatsAppLocator
  Copy/        CopyPlan (enumerate), Copier (resume, progress, cancel)
  Exporter/    command line, process runner, progress parser, wrong-key detection
  Archive/     chats.js writer + stats, media move, members.js (SQLite + vcf + wa.db), cleanup
  Pipeline/    CopySession (pause/resume on unplug), BuildArchive (step 6)
  Util/        strings, logging with key redaction, free space, OneDrive, country codes
src/App/       WinUI 3 app: one window, one panel per wizard step
src/Cli/       chatkeeper-cli: the whole pipeline from a folder (tests, support)
viewer/        index.html, supplied, shipped unchanged
exporter/      wtsexporter.exe + LICENSE, fetched by tools/fetch-exporter.ps1 (not in git)
tests/         unit tests, synthetic crypt15 fixture, end-to-end and viewer tests
installer/     Inno Setup script (per-user setup .exe)
tools/         generators for the .vcxproj, .resw strings and placeholder images
```

## Build (Windows)

Visual Studio 2022 with "Desktop development with C++" and the Windows App SDK
C++ templates, plus vcpkg (`vcpkg integrate install`).

```
pwsh tools/fetch-exporter.ps1            # wtsexporter 0.13.0 into exporter\ (verifies attestation)
nuget restore ChatKeeper.sln
msbuild ChatKeeper.sln /p:Configuration=Release /p:Platform=x64
iscc /DSourceDir=<output folder> installer\ChatKeeper.iss   # optional installer
```

The app is unpackaged and self-contained (`WindowsPackageType=None`,
`WindowsAppSDKSelfContained=true`), pinned to Windows App SDK 1.8.260921001
(the newest 1.x). Because it uses `packages.config`, every 1.8 component
package is listed and imported explicitly; regenerate the project with
`python tools/make_vcxproj.py` after changing packages or adding sources.

## Tests

```
cmake -S . -B build -G Ninja && cmake --build build && ./build/core-tests
pip install "whatsapp-chat-exporter[crypt15]==0.13.0" pycryptodome
(cd tests/viewer && npm install)
python tests/e2e/run_e2e.py --cli build/chatkeeper-cli --exporter wtsexporter --viewer-check
```

`tests/fixtures/make_fixture.py` builds a phone-shaped `WhatsApp/` folder with
real crypt15 backups (encrypted exactly as wtsexporter expects) and a known test
key. CI (`.github/workflows/build.yml`) runs all of this on Linux, builds the
WinUI app on Windows and runs the end-to-end test with the official
`wtsexporter.exe`.

## Renaming the app

`ChatKeeper` appears in `src/App/AppInfo.h`, `tools/make_resw.py` (`AppName`)
and `installer/ChatKeeper.iss`.
