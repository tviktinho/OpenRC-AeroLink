import serial, sys, time
import pyvjoy
import keyboard
from collections import deque

SERIAL_PORT = "COM10"   # confira a porta!
BAUD = 115200
TIMEOUT_S = 1.0

# ----- TECLAS map -----
SW1_ON_KEY  = 'g'
SW1_OFF_KEY = 'g'
SW2_ON_KEY  = 'f'
SW2_OFF_KEY = 'f'

# ----- vJoy -----
VJOY_DEVICE_ID = 1
BTN_S1 = 9
BTN_S2 = 10

# ----- FILTRO DE SWITCHES -----
SW_LOW_THR  = 64
SW_HIGH_THR = 192
DEBOUNCE_MS = 1
STICKY_MS   = 150
SW_WINDOW_N = 5

# ----- EIXOS (8 pots) -----gf
DEADZONE_255 = 1
SMOOTH_N = 0
AXIS_ORDER = [
    ('wAxisX',    0),
    ('wAxisY',    1),
    ('wAxisZ',    2),
    ('wAxisXRot', 3),
    ('wAxisYRot', 4),
    ('wAxisZRot', 5),
    ('wSlider',   6),
    ('wDial',     7),
]
INVERT = [False]*8

def to_vjoy(val_255: int, invert=False) -> int:
    v = 0 if val_255 < 0 else 255 if val_255 > 255 else val_255
    if abs(v - 127.5) <= DEADZONE_255: v = 128
    if invert: v = 255 - v
    out = int(round((v / 255.0) * 32767.0))
    return 1 if out < 1 else 32767 if out > 32767 else out

def tap_key(key_name: str):
    try:
        keyboard.press_and_release(key_name)
    except Exception as e:
        print(f"[WARN] Falha ao enviar tecla '{key_name}': {e}", file=sys.stderr)

def median_of(deq):
    if not deq:
        return 0
    s = sorted(deq)
    n = len(s)
    return s[n//2] if n % 2 == 1 else (s[n//2 - 1] + s[n//2]) // 2

class SwitchFilter:
    def __init__(self, low=SW_LOW_THR, high=SW_HIGH_THR, debounce_ms=DEBOUNCE_MS, sticky_ms=STICKY_MS, window_n=SW_WINDOW_N):
        self.low = low
        self.high = high
        self.debounce = debounce_ms / 1000.0
        self.sticky = sticky_ms / 1000.0
        self.samples = deque(maxlen=window_n)
        self.last_stable = 0
        self.candidate = 0
        self.cand_since = 0.0
        self.last_change = 0.0

    def feed(self, raw_val, now):
        self.samples.append(int(raw_val))
        med = median_of(self.samples)

        if med <= self.low:
            cand = 0
        elif med >= self.high:
            cand = 1
        else:
            cand = self.candidate  # zona morta

        if cand != self.candidate:
            self.candidate = cand
            self.cand_since = now

        if (now - self.last_change) < self.sticky:
            return self.last_stable

        if (now - self.cand_since) >= self.debounce and self.last_stable != self.candidate:
            self.last_stable = self.candidate
            self.last_change = now

        return self.last_stable

def main():
    print(f">> Iniciando… vJoy#{VJOY_DEVICE_ID}, Serial {SERIAL_PORT} @ {BAUD}")
    # vJoy
    try:
        j = pyvjoy.VJoyDevice(VJOY_DEVICE_ID)
        print(">> vJoy OK.")
    except Exception as e:
        print("[ERRO] Não abriu vJoy. Abra o vJoy Monitor/Config e confira o Device.", e, file=sys.stderr)
        return

    # Serial
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD, timeout=TIMEOUT_S)
        print(f">> Serial OK em {SERIAL_PORT}.")
    except serial.SerialException as e:
        print("[ERRO] Porta serial:", e, file=sys.stderr)
        return
    ser.reset_input_buffer()

    smooth = [deque(maxlen=max(1, SMOOTH_N)) for _ in range(8)]
    sw1 = SwitchFilter()
    sw2 = SwitchFilter()
    last_s1 = None
    last_s2 = None
    last_ok = time.time()

    print(">> Rodando. Esperado: p0..p7,s1,s2 (10 campos).")
    print(f">> s1 -> vJoy Btn {BTN_S1} (hold) + tecla {SW1_ON_KEY}/{SW1_OFF_KEY}")
    print(f">> s2 -> vJoy Btn {BTN_S2} (hold) + tecla {SW2_ON_KEY}/{SW2_OFF_KEY}")

    try:
        while True:
            line = ser.readline().decode(errors='ignore').strip()
            now = time.time()

            if not line:
                if now - last_ok > 1.0:
                    for name, _ in AXIS_ORDER:
                        setattr(j.data, name, to_vjoy(128))
                    j.update()
                continue

            parts = line.split(',')
            if len(parts) != 10:
                # print(f"[WARN] Pacote com {len(parts)} campos: {line}")
                continue

            try:
                vals = [int(x) for x in parts]
            except ValueError:
                # print(f"[WARN] Valor inválido: {line}")
                continue

            pots = vals[:8]
            s1_raw, s2_raw = vals[8], vals[9]

            # Eixos
            for i, (name, src_idx) in enumerate(AXIS_ORDER):
                v = pots[src_idx]
                smooth[i].append(v)
                if SMOOTH_N > 0:
                    v = sum(smooth[i]) / len(smooth[i])
                setattr(j.data, name, to_vjoy(int(round(v)), invert=INVERT[i]))
            j.update()
            last_ok = now

            # Switches com filtro robusto
            s1_state = sw1.feed(s1_raw, now)
            s2_state = sw2.feed(s2_raw, now)

            # vJoy HOLD estável
            j.set_button(BTN_S1, 1 if s1_state else 0)
            j.set_button(BTN_S2, 1 if s2_state else 0)
            j.update()

            # Teclas (apenas na borda)
            if last_s1 is None or s1_state != last_s1:
                tap_key(SW1_ON_KEY if s1_state else SW1_OFF_KEY)
                last_s1 = s1_state
            if last_s2 is None or s2_state != last_s2:
                tap_key(SW2_ON_KEY if s2_state else SW2_OFF_KEY)
                last_s2 = s2_state

            # print(f"s1_raw={s1_raw} s1={s1_state} | s2_raw={s2_raw} s2={s2_state}")

    except KeyboardInterrupt:
        print("\n>> Encerrando…")
    except serial.SerialException as e:
        print("[ERRO] Serial durante leitura:", e, file=sys.stderr)
    finally:
        try:
            ser.close()
        except Exception:
            pass
        # não “soltamos” os botões aqui de propósito; mantemos último estado

if __name__ == "__main__":
    main()