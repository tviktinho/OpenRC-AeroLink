param(
    [string]$Name = "OpenRC-AeroLink-Bridge",
    [switch]$UseGlobal
)

$ErrorActionPreference = 'Stop'

# Build in the script's directory to avoid cluttering repo root
Push-Location $PSScriptRoot

# Python environment setup
if ($UseGlobal) {
  # Use currently active/global Python
  $py = 'python'
  Write-Host "Using global Python (no venv)" -ForegroundColor DarkCyan
} else {
  # Use an isolated venv so we fully control package versions (esp. PySimpleGUI)
  $venv = Join-Path $PSScriptRoot '.venv_build'
  $py = Join-Path $venv 'Scripts/python.exe'
  $pipProbe = Join-Path $venv 'Scripts/pip.exe'
  if (Test-Path $pipProbe) {
    Write-Host "Reusing build venv: $venv" -ForegroundColor DarkCyan
  } else {
    Write-Host "Creating build venv..." -ForegroundColor Cyan
    try { Remove-Item -Recurse -Force "$venv" -ErrorAction SilentlyContinue } catch {}
    python -m venv "$venv"
  }
}

if ($UseGlobal) {
  Write-Host "Installing requirements (global env)..." -ForegroundColor Cyan
} else {
  Write-Host "Installing requirements in venv..." -ForegroundColor Cyan
}
& $py -m pip install --upgrade pip
# Install from PyPI (uses FreeSimpleGUI, tkinter comes with Python runtime)
& $py -m pip install --no-cache-dir --upgrade -r "$(Join-Path $PSScriptRoot requirements.txt)"

Write-Host "Building single-file EXE (clean)..." -ForegroundColor Cyan
& $py -m PyInstaller --clean --noconfirm --onefile --noconsole `
  --name "$Name" `
  --distpath "$(Join-Path $PSScriptRoot dist)" `
  --workpath "$(Join-Path $PSScriptRoot build)" `
  --specpath "$PSScriptRoot" `
  --hidden-import serial.tools.list_ports `
  --collect-all FreeSimpleGUI `
  --hidden-import tkinter `
  --hidden-import _tkinter `
  --collect-all tkinter `
  --collect-all tcl `
  --icon "$(Join-Path $PSScriptRoot app\\icon.ico)" `
  --add-data "$(Join-Path $PSScriptRoot app\\icon.ico);." `
  "$(Join-Path $PSScriptRoot app\main.py)"

Write-Host "Cleaning intermediate build artifacts..." -ForegroundColor DarkGray
try { Remove-Item -Recurse -Force "$(Join-Path $PSScriptRoot build)" -ErrorAction SilentlyContinue } catch {}
try { Remove-Item -Force    "$(Join-Path $PSScriptRoot $Name.spec)" -ErrorAction SilentlyContinue } catch {}

Pop-Location

Write-Host "Done. EXE at: software\\pc\\dist\\$Name.exe" -ForegroundColor Green
