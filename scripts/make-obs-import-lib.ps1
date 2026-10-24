# Create obs.lib (import library) from an installed OBS Studio, so the plugin can be built
# without building OBS. Run from a "Developer PowerShell for Visual Studio".
#   .\scripts\make-obs-import-lib.ps1 -ObsDir "C:\Program Files\obs-studio" -OutDir build\obs-lib
param(
    [string]$ObsDir = "C:\Program Files\obs-studio",
    [string]$OutDir = "build\obs-lib"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$dll = Join-Path $ObsDir 'bin\64bit\obs.dll'
$exports = & dumpbin.exe /exports $dll
$names = @()
$started = $false
foreach ($line in $exports) {
    if ($line -match 'ordinal\s+hint\s+RVA') { $started = $true; continue }
    if ($started) {
        if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $names += $Matches[1] }
        elseif ($line.Trim() -eq 'Summary') { break }
    }
}
$def = Join-Path $OutDir 'obs.def'
"LIBRARY obs.dll`nEXPORTS" | Set-Content $def
$names | ForEach-Object { "    $_" } | Add-Content $def
& lib.exe /nologo /def:$def /machine:x64 /out:(Join-Path $OutDir 'obs.lib')
Write-Host "Created $(Join-Path $OutDir 'obs.lib') with $($names.Count) exports"
