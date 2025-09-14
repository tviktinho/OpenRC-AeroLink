OpenRC-AeroLink Bridge (PC App)

Resumo
- App Windows (.exe único) que lê dados da porta serial do MEGA, envia eixos para o vJoy e dispara teclas nas bordas dos switches.
- GUI simples: selecionar porta COM e definir teclas dos dois switches (ex.: G e R).

Pré-requisitos de sistema
- vJoy instalado e dispositivo configurado (ID 1 por padrão).
- Driver do vJoy funcionando (o app usa a biblioteca pyvjoy).
- Para envio de teclas, pode ser necessário executar como Administrador (lib keyboard).

Como executar via Python (desenvolvimento)
1) `pip install -r software/pc/requirements.txt`
2) `python software/pc/app/main.py`

Gerar .exe (Windows)
1) `pip install -r software/pc/requirements.txt`
2) `powershell software/pc/build_exe.ps1`
3) O executável ficará em `dist/OpenRC-AeroLink-Bridge.exe`.

Notas
- Configurações (porta, teclas) são salvas em `%APPDATA%/OpenRC-AeroLink/config.json`.
- O app tenta listar portas automaticamente (botão Atualizar).
- Futuras expansões: tela de calibração e visualização dos 8 potenciômetros podem ser incluídas nesta mesma GUI sem quebrar o fluxo atual.

