param(
  [switch]$All
)

$ErrorActionPreference = 'Stop'

function Remove-Safe([string]$path) {
  if (Test-Path $path) {
    Write-Host "Removing $path" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $path -ErrorAction SilentlyContinue
  }
}

# Clean artifacts under repo root
Remove-Safe (Join-Path $PSScriptRoot '..\\..\\build')
Remove-Safe (Join-Path $PSScriptRoot '..\\..\\dist')
Remove-Safe (Join-Path $PSScriptRoot '..\\..\\OpenRC-AeroLink-Bridge.spec')
Remove-Safe (Join-Path $PSScriptRoot '..\\..\\.tmp_psg')
Remove-Safe (Join-Path $PSScriptRoot '..\\..\\.tmp_psg2')

# Clean artifacts under software/pc
Remove-Safe (Join-Path $PSScriptRoot 'build')
Remove-Safe (Join-Path $PSScriptRoot 'OpenRC-AeroLink-Bridge.spec')

if ($All) {
  # Optionally remove dist and venv to leave only sources
  Remove-Safe (Join-Path $PSScriptRoot 'dist')
  Remove-Safe (Join-Path $PSScriptRoot '.venv_build')
}

Write-Host "Cleanup done." -ForegroundColor Green

