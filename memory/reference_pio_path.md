---
name: reference-pio-path
description: PlatformIO está instalado mas NÃO está no PATH do PowerShell — usar caminho absoluto ou alias.
metadata:
  type: reference
---

No PC do usuário, o `pio.exe` está em:

```
C:\Users\Administrator\.platformio\penv\Scripts\pio.exe
```

(instalação típica via extension PlatformIO IDE do VS Code).

**NÃO está no PATH do PowerShell por default** — chamar `pio` direto retorna `CommandNotFoundException`.

**Como contornar (em ordem de praticidade):**

1. Caminho absoluto único:
   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload
   ```
2. Alias pra sessão:
   ```powershell
   Set-Alias pio "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
   ```
3. Adicionar ao PATH (permanente, reabrir terminal depois):
   ```powershell
   [Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";$env:USERPROFILE\.platformio\penv\Scripts", "User")
   ```

**How to apply:** ao sugerir comandos PIO pro usuário neste PC, NÃO usar `pio` cru — usar caminho absoluto ou lembrá-lo de criar o alias. Em scripts/exemplos copy-paste pra ele, preferir a forma com `&` + path completo, que sempre funciona.
