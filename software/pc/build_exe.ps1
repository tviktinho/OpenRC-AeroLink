param(
    [string]$Name = "OpenRC-AeroLink-Bridge"
)

Write-Host "Installing requirements..." -ForegroundColor Cyan
python -m pip install --upgrade pip
pip install -r "$(Join-Path $PSScriptRoot requirements.txt)"

Write-Host "Building single-file EXE..." -ForegroundColor Cyan
pyinstaller --noconfirm --onefile --noconsole `
  --name "$Name" `
  --hidden-import serial.tools.list_ports `
  "$(Join-Path $PSScriptRoot app\main.py)"

Write-Host "Done. EXE at: dist\$Name.exe" -ForegroundColor Green

