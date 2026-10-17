<# :
@echo off
rem EnshroudedBaseCap installer / uninstaller. Run it from the Enshrouded folder,
rem next to enshrouded.exe.
rem   install_basecap.bat    adds EnshroudedBaseCap.dll to the Addons= line of
rem                          EnshroudedVKFG.ini so the frame generation mod loads it.
rem   uninstall_basecap.bat  (the same script under that name) takes it out of
rem                          Addons= again and deletes the add-on's files.
rem Nothing else is changed. The PowerShell below does the work.
setlocal
set "BASECAP_DIR=%~dp0"
set "BASECAP_SELF=%~f0"
set "BASECAP_MODE=install"
if /i "%~n0"=="uninstall_basecap" set "BASECAP_MODE=uninstall"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-Expression ([IO.File]::ReadAllText($env:BASECAP_SELF))"
echo.
pause
exit /b
#>
$ErrorActionPreference = 'Stop'
$dir = $env:BASECAP_DIR
$install = $env:BASECAP_MODE -eq 'install'
$ini = Join-Path $dir 'EnshroudedVKFG.ini'
$self = 'EnshroudedBaseCap.dll'

function Stop-WithMessage([string]$msg) { Write-Host $msg -ForegroundColor Red; exit 1 }

function Test-GameRunning {
    $exe = Join-Path $dir 'enshrouded.exe'
    return [bool](Get-Process -Name enshrouded -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
}

if (-not (Test-Path (Join-Path $dir 'enshrouded.exe'))) {
    Stop-WithMessage "Run this from the Enshrouded game folder, next to enshrouded.exe."
}

# Read the ini as bytes so its encoding and line endings survive the edit.
function Read-Ini {
    $bytes = [IO.File]::ReadAllBytes($ini)
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) { $enc = New-Object Text.UnicodeEncoding($false, $true) }
    elseif ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) { $enc = New-Object Text.UTF8Encoding($true) }
    else { $enc = New-Object Text.UTF8Encoding($false) }
    $text = [IO.File]::ReadAllText($ini, $enc)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    return @{ Enc = $enc; NL = $nl; Lines = [Collections.Generic.List[string]]($text -split "\r?\n") }
}

function Write-Ini($state) {
    [IO.File]::WriteAllText($ini, ($state.Lines -join $state.NL), $state.Enc)
}

# Callers wrap the result in @(): PowerShell returns a one-item list as a bare string.
function Get-Addons([string]$value) {
    return @($value -split '[,;]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

if ($install) {
    if (-not (Test-Path (Join-Path $dir $self))) { Stop-WithMessage "$self is not in this folder. Extract all files from the zip here first." }
    if (-not (Test-Path $ini)) {
        Stop-WithMessage "EnshroudedVKFG.ini was not found. Install 'Enshrouded DLSS Frame Generation' (Nexus Mods, Enshrouded mod 141) first."
    }
    $state = Read-Ini
    $lines = $state.Lines
    $at = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\s*Addons\s*=(.*)$') { $at = $i; $value = $Matches[1]; break } }
    if ($at -ge 0) {
        $addons = @(Get-Addons $value)
        if ($addons -contains $self) {
            Write-Host "Already installed: EnshroudedVKFG.ini already loads $self." -ForegroundColor Green
            exit 0
        }
        $lines[$at] = 'Addons=' + (($addons + $self) -join ',')
    } else {
        $section = -1
        for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\s*\[EnshroudedVKFG\]\s*$') { $section = $i; break } }
        if ($section -lt 0) { Stop-WithMessage "EnshroudedVKFG.ini has no [EnshroudedVKFG] section. Reinstall the frame generation mod." }
        $lines.Insert($section + 1, "Addons=$self")
    }
    Copy-Item $ini "$ini.bak-basecap" -Force
    Write-Ini $state
    Write-Host "Installed. EnshroudedVKFG.ini now loads $self." -ForegroundColor Green
    Write-Host "(Backup of the previous file: EnshroudedVKFG.ini.bak-basecap)"
    Write-Host ""
    Write-Host "Next:"
    Write-Host " - Turn off any frame cap for Enshrouded in the NVIDIA Control Panel / NVIDIA App or RTSS."
    Write-Host "   Those cap the final output and would sit on top of this one."
    Write-Host " - Start the game. F9 / F11 lower / raise the base cap (default 40)."
    if (Test-GameRunning) {
        Write-Host " - Enshrouded is running: restart it to load the cap." -ForegroundColor Yellow
    }
} else {
    if (Test-GameRunning) {
        Stop-WithMessage "Close Enshrouded first: the game keeps $self open while it runs."
    }
    if (Test-Path $ini) {
        $state = Read-Ini
        $lines = $state.Lines
        $changed = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match '^\s*Addons\s*=(.*)$') {
                $addons = @(Get-Addons $Matches[1])
                if ($addons -contains $self) {
                    $lines[$i] = 'Addons=' + ((@($addons | Where-Object { $_ -ne $self })) -join ',')
                    $changed = $true
                }
            }
        }
        if ($changed) { Write-Ini $state; Write-Host "Removed $self from the Addons line of EnshroudedVKFG.ini." }
    }
    foreach ($f in @($self, 'EnshroudedBaseCap.ini', 'EnshroudedBaseCap.log', 'EnshroudedVKFG.ini.bak-basecap', 'install_basecap.bat')) {
        $p = Join-Path $dir $f
        if (Test-Path $p) { Remove-Item $p -Force; Write-Host "  removed $f" }
    }
    $docs = Join-Path $dir 'EnshroudedBaseCap-docs'
    if (Test-Path $docs) { Remove-Item $docs -Recurse -Force; Write-Host "  removed EnshroudedBaseCap-docs\" }
    Write-Host "EnshroudedBaseCap is uninstalled. You can delete uninstall_basecap.bat too." -ForegroundColor Green
}
