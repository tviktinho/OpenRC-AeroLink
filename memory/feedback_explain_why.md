---
name: feedback-explain-why
description: Usuário quer entender o porquê das coisas, não só receber código pronto — é projeto de aprendizado (TCC)
metadata:
  type: feedback
---

Para o projeto OpenRC-AeroLink/TCC ([[project-openrc-aerolink]], [[user-profile]]), explicar **o porquê das decisões técnicas**, não só dar a solução.

**Why:** é o TCC do usuário; ele disse explicitamente "quero ENTENDER os protocolos, não só copiar. Explique o porquê das coisas." Ele precisa defender o trabalho academicamente e quer construir o conhecimento, não só ter um firmware funcionando.

**How to apply:**
- Antes de escrever código para um protocolo RC, explicar em alto nível como o protocolo funciona (endereço, canais de RF / frequency hopping, formato do payload, sequência de bind, failsafe).
- Justificar escolhas de biblioteca / abordagem (ex.: "por que SPI direto em vez de RF24.write() para XN297").
- Quando uma decisão tiver trade-off, mostrar as alternativas e por que a escolhida ganhou.
- Não vale apenas colar código de `nRF24_multipro` sem comentar — anotar o que cada bloco está fazendo no contexto do protocolo.
- Defaults: respostas curtas para perguntas simples, mas com explicação técnica quando o tópico for RF/protocolo (porque é o foco do aprendizado).
