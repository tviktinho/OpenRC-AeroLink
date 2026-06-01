# Memory Index

- [User profile](user_profile.md) — estudante de engenharia / TCC; conforto com Arduino e eletrônica embarcada, novo em RF.
- [Project: OpenRC-AeroLink](project_openrc_aerolink.md) — visão geral do projeto e do hardware do NANO_TX (pinagem, RF, payload atual).
- [Subproject: Nano multiprotocol drone TX](project_nano_multiprotocol_drone_tx.md) — nova iniciativa: emular protocolos de RC de drone (Bayang/SymaX/V2x2/etc) no Nano TX. BLOQUEADO em identificação de hardware.
- [User's existing drone + QX7](project_user_existing_drone_qx7.md) — Mobula no QX7, chip CC2500 confirmado (FrSky D8). Modelo exato da Mobula ainda pendente.
- [User hardware inventory](project_user_hardware_inventory.md) — tabela do hardware RF do usuário (Nano+nRF24, Heltec V2, QX7, Mobula) e o que cada um cobre/não cobre.
- [Hardware RF chip constraint](hardware_rf_chip_constraint.md) — nRF24 só transmite nRF24/XN297; FrSky/Flysky/Spektrum exigem CC2500/A7105/CYRF6936.
- [RF emulation boundaries](reference_rf_emulation_boundaries.md) — por que XN297 cabe e FrSky D8 não cabe no nRF24 (PHY); material de fundamentação do TCC.
- [PIO path](reference_pio_path.md) — pio.exe não está no PATH; usar caminho absoluto ou Set-Alias.
- [Matek F411-RX pads](reference_matek_f411rx_pads.md) — pinout dos pads expostos; SBUS é OUT, entrada SBUS externa só pelo R2.
- [Betaflight resource conflicts](reference_betaflight_resource_conflicts.md) — pinos PWM/PPM duplicados com UART bloqueiam SBUS; armadilha clássica no MATEKF411RX.
- [Feedback: explain the why](feedback_explain_why.md) — projeto é de aprendizado; explicar a teoria por trás das decisões, não só dar código. **Vale só pro TCC propriamente dito.**
- [Feedback: tom direto no hobby SBUS](feedback_direct_tone_hobby.md) — receptor SBUS é hobby, não TCC; tom direto, sem teoria.
