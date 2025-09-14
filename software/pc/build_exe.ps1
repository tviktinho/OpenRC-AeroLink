param(
    [string]$Name = "OpenRC-AeroLink-Bridge"
)

$ErrorActionPreference = 'Stop'

# Build in the script's directory to avoid cluttering repo root
Push-Location $PSScriptRoot

# Use an isolated venv so we fully control package versions (esp. PySimpleGUI)
$venv = Join-Path $PSScriptRoot '.venv_build'
if (Test-Path $venv) {
  Write-Host "Reusing build venv: $venv" -ForegroundColor DarkCyan
} else {
  Write-Host "Creating build venv..." -ForegroundColor Cyan
  python -m venv "$venv"
}

$py = Join-Path $venv 'Scripts/python.exe'
$pip = Join-Path $venv 'Scripts/pip.exe'

Write-Host "Installing requirements in venv..." -ForegroundColor Cyan
& $py -m pip install --upgrade pip
# Ensure we get the real PySimpleGUI from the private index
try { & $pip uninstall -y PySimpleGUI | Out-Null } catch {}
& $pip install --no-cache-dir --force-reinstall `
    --index-url https://PySimpleGUI.net/install `
    --extra-index-url https://pypi.org/simple `
    PySimpleGUI
# Install remaining deps (and PySimpleGUI if not covered above)
& $pip install --no-cache-dir --upgrade `
    --index-url https://PySimpleGUI.net/install `
    --extra-index-url https://pypi.org/simple `
    -r "$(Join-Path $PSScriptRoot requirements.txt)"

Write-Host "Building single-file EXE (clean)..." -ForegroundColor Cyan
& $py -m PyInstaller --clean --noconfirm --onefile --noconsole `
  --name "$Name" `
  --distpath "$(Join-Path $PSScriptRoot dist)" `
  --workpath "$(Join-Path $PSScriptRoot build)" `
  --specpath "$PSScriptRoot" `
  --hidden-import serial.tools.list_ports `
  --hidden-import PySimpleGUI.PySimpleGUI `
  --collect-all PySimpleGUI `
  --hidden-import tkinter `
  --hidden-import _tkinter `
  --collect-all tkinter `
  --collect-all tcl `
  "$(Join-Path $PSScriptRoot app\main.py)"

Write-Host "Cleaning intermediate build artifacts..." -ForegroundColor DarkGray
try { Remove-Item -Recurse -Force "$(Join-Path $PSScriptRoot build)" -ErrorAction SilentlyContinue } catch {}
try { Remove-Item -Force    "$(Join-Path $PSScriptRoot $Name.spec)" -ErrorAction SilentlyContinue } catch {}

Pop-Location

Write-Host "Done. EXE at: software\\pc\\dist\\$Name.exe" -ForegroundColor Green
