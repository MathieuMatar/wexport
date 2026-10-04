# Bundled exporter

The app ships the official **WhatsApp Chat Exporter** (`wtsexporter`, MIT) Windows
build here, next to `ChatKeeper.exe`:

```
exporter\wtsexporter.exe
exporter\LICENSE
exporter\VERSION
```

It isn't checked in. Get it with:

```
pwsh tools/fetch-exporter.ps1        # pinned to 0.13.0
```

The script downloads the release asset, verifies its GitHub artifact attestation
(`gh attestation verify`), and checks `--help`. CI then runs a real crypt15
decrypt with it (`tests/e2e/run_e2e.py`) to prove crypt15 support.
