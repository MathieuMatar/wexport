# Downloads the pinned official wtsexporter Windows build into exporter\,
# verifies its GitHub artifact attestation, and checks that it runs.
# Needs the GitHub CLI (gh) signed in, or GH_TOKEN set (CI).
#
#   pwsh tools/fetch-exporter.ps1 [-Version 0.13.0]
param(
    [string]$Version = "0.13.0",
    [string]$Repo = "KnugiHK/WhatsApp-Chat-Exporter"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "exporter"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("wtsexporter-" + $Version)
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

Write-Host "Release assets for $Repo $Version:"
$assets = (gh release view $Version -R $Repo --json assets | ConvertFrom-Json).assets
$assets | ForEach-Object { Write-Host ("  " + $_.name + "  (" + $_.size + " bytes)") }

# Pick the Windows x64 build: an .exe, or a zip containing one.
$pick = $assets | Where-Object { $_.name -match '(?i)win' -and $_.name -match '(?i)\.exe$' -and $_.name -notmatch '(?i)arm' } | Select-Object -First 1
if (-not $pick) { $pick = $assets | Where-Object { $_.name -match '(?i)\.exe$' } | Select-Object -First 1 }
if (-not $pick) { $pick = $assets | Where-Object { $_.name -match '(?i)win' -and $_.name -match '(?i)\.zip$' } | Select-Object -First 1 }
if (-not $pick) { throw "No Windows build found among the release assets above. Stop and ask." }
Write-Host "Using $($pick.name)"
gh release download $Version -R $Repo -p $pick.name -D $tmp --clobber
$file = Join-Path $tmp $pick.name

# Provenance: the release workflow attests its artifacts (see the project README).
gh attestation verify $file -R $Repo
if ($LASTEXITCODE -ne 0) { throw "Artifact attestation check failed for $($pick.name)" }

if ($file -match '\.zip$') {
    Expand-Archive -Path $file -DestinationPath (Join-Path $tmp "x") -Force
    $exe = Get-ChildItem -Recurse (Join-Path $tmp "x") -Filter *.exe | Select-Object -First 1
    if (-not $exe) { throw "No .exe inside $($pick.name)" }
    $file = $exe.FullName
}
Copy-Item $file (Join-Path $dest "wtsexporter.exe") -Force
gh api "repos/$Repo/contents/LICENSE?ref=$Version" -H "Accept: application/vnd.github.raw" > (Join-Path $dest "LICENSE")
Set-Content -Path (Join-Path $dest "VERSION") -Value "wtsexporter $Version ($($pick.name))"

$env:PYTHONUTF8 = "1"
$help = & (Join-Path $dest "wtsexporter.exe") --help 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "wtsexporter --help failed:`n$help" }
foreach ($flag in @("--no-banner", "--wab", "--enrich-from-vcards", "--default-country-code", "--no-html", "Crypt15")) {
    if ($help -notmatch [regex]::Escape($flag)) { throw "wtsexporter $Version --help doesn't mention $flag" }
}
if ($help -notmatch [regex]::Escape($Version)) { Write-Warning "--help doesn't show version $Version" }
Write-Host "wtsexporter $Version is in $dest"
