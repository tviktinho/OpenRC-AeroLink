OpenRC-AeroLink Bridge (PC App)

Resumo
- App Windows (EXE único) que lê dados da serial do MEGA, envia eixos para o vJoy e dispara teclas nas bordas dos switches.
- GUI simples: selecionar porta COM e definir teclas dos dois switches (ex.: G e R).

Pré‑requisitos
- vJoy instalado e dispositivo configurado (ID 1 por padrão).
- Driver do vJoy funcionando (o app usa a biblioteca pyvjoy).
- Para envio de teclas, pode ser necessário executar como Administrador (lib keyboard).

Execução via Python (desenvolvimento)
1) `pip install -r software/pc/requirements.txt`
2) `python software/pc/app/main.py`

Gerar .exe (Windows)
1) `powershell software/pc/build_exe.ps1`
2) O executável ficará em `software/pc/dist/OpenRC-AeroLink-Bridge.exe`.
3) O script remove automaticamente arquivos temporários de build e o `.spec` (mantém só o `.exe`).

Limpeza de artefatos
- Para remover pastas antigas `build/`, `dist/`, `.spec` e venvs de teste: `powershell software/pc/clean_artifacts.ps1`
- Para limpar tudo (inclusive `software/pc/dist` e o venv de build): `powershell software/pc/clean_artifacts.ps1 -All`

Notas
- Configurações (porta, teclas) são salvas em `%APPDATA%/OpenRC-AeroLink/config.json`.
- O app lista portas automaticamente (botão Atualizar).
