---
name: feedback-direct-tone-hobby
description: No subprojeto do receptor SBUS (hobby, não TCC), tom direto — sem teoria, sem porquês, só ações.
metadata:
  type: feedback
---

**Contexto:** o subprojeto `firmware/ESP32C3_RX_SBUS/` é hobby do usuário pra pilotar a Mobula com o NANO_TX V2 — **não entra no TCC** ([[project-nano-multiprotocol-drone-tx]]).

**Feedback explícito do usuário (2026-05-31):** "seja mais direto e menos didático, quero apenas que funcione com meu controle que é meu tcc. caso necessário posteriormente criaremos a documentação do que foi feito".

**How to apply ao trabalhar no receptor SBUS:**
- Responder em comandos diretos, listas curtas, próximo passo
- NÃO explicar por que funciona, NÃO comparar protocolos, NÃO dar "lição pro TCC"
- Pular tabelas comparativas e analogias didáticas
- Cortar "Sobre [tópico]" e seções de fundamentação
- Manter só: pergunta de status, próxima ação, comando exato

**Exceção:** o feedback educacional ([[feedback-explain-why]]) **continua valendo pra qualquer coisa relacionada ao TCC propriamente dito** — NANO_TX V2, protocolo nRF24, fundamentação de RF. Mudou só o tom **dentro do subprojeto SBUS**.

**How to apply:** se a conversa atual está no escopo `ESP32C3_RX_SBUS` (firmware do receptor, config Betaflight, debug físico do drone) → tom direto. Se voltar pro TCC ou for sobre conceito de RF → tom didático normal.
