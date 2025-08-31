import serial, sys, time
import pyvjoy
import keyboard  # pip install keyboard
from collections import deque

SERIAL_PORT = "COM10"
BAUD = 115200
TIMEOUT_S = 1.0

# --- TECLAS mapeadas para os switches ---
# s1: 0->OFF, 255->ON
SW1_ON_KEY  = 'g'
SW1_OFF_KEY = 'g'
# s2: 0->OFF, 255->ON
SW2_ON_KEY  = 'f'
SW2_OFF_KEY = 'f'

# --- BOTÕES vJoy para os switches ---
VJOY_DEVICE_ID = 1
BTN_S1 = 1     # ajuste conforme seu vJoy (precisa existir!)
BTN_S2 = 2

# Limiar para considerar ON (>=128 é ON)
ON_THRESHOLD = 10

# --- ajustes de resposta dos eixos (vJoy) ---gf
DEADZONE_255 = 1
SMOOTH_N = 0

# 8 eixos do vJoy (apenas os potenciômetros p0..p7)
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
    if val_255 < 0:   val_255 = 0
    if val_255 > 255: val_255 = 255
    if abs(val_255 - 127.5) <= DEADZONE_255:
        val_255 = 128
    if invert:
        val_255 = 255 - val_255
    out = int(round((val_255 / 255.0) * 32767.0))
    return max(1, min(32767, out))

def tap_key(key_name: str):
    try:
        keyboard.press_and_release(key_name)
    except Exception as e:
        print(f"[WARN] Falha ao enviar tecla '{key_name}': {e}", file=sys.stderr)

def main():
    # vJoy
    j = pyvjoy.VJoyDevice(VJOY_DEVICE_ID)

    # Serial
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD, timeout=TIMEOUT_S)
    except serial.SerialException as e:
        print("ERRO ao abrir porta serial:", e, file=sys.stderr)
        return
    ser.reset_input_buffer()

    smooth = [deque(maxlen=max(1, SMOOTH_N)) for _ in range(8)]
    last_ok = time.time()

    # estados anteriores (para gerar “clique” só na borda)
    last_s1_on = None
    last_s2_on = None

    print(f">> Eixos: p0..p7  |  Switches -> vJoy(btn hold) + teclas")
    print(f"   s1: ON->{SW1_ON_KEY} OFF->{SW1_OFF_KEY}  |  vJoy Btn {BTN_S1}")
    print(f"   s2: ON->{SW2_ON_KEY} OFF->{SW2_OFF_KEY}  |  vJoy Btn {BTN_S2}")

    try:
        while True:
            line = ser.readline().decode(errors='ignore').strip()
            now = time.time()

            if not line:
                if now - last_ok > 1.0:
                    for name, _ in AXIS_ORDER:
                        setattr(j.data, name, to_vjoy(128))
                    # solta botões por segurança quando parar de receber
                    j.set_button(BTN_S1, 0)
                    j.set_button(BTN_S2, 0)
                    j.update()
                continue

            parts = line.split(',')
            if len(parts) != 10:
                continue

            try:
                vals = [int(x) for x in parts]
            except ValueError:
                continue

            pots = vals[:8]
            s1_raw, s2_raw = vals[8], vals[9]
            s1_on = 1 if s1_raw >= ON_THRESHOLD else 0
            s2_on = 1 if s2_raw >= ON_THRESHOLD else 0

            # ---- EIXOS (8 pots) ----
            for i, (name, src_idx) in enumerate(AXIS_ORDER):
                v = pots[src_idx]
                smooth[i].append(v)
                if SMOOTH_N > 0:
                    v = sum(smooth[i]) / len(smooth[i])
                setattr(j.data, name, to_vjoy(int(round(v)), invert=INVERT[i]))

            # ---- BOTÕES vJoy (hold) ----
            j.set_button(BTN_S1, s1_on)
            j.set_button(BTN_S2, s2_on)

            j.update()
            last_ok = now

            # ---- TECLAS nas transições (edge-trigger) ----
            if last_s1_on is None or s1_on != last_s1_on:
                tap_key(SW1_ON_KEY if s1_on else SW1_OFF_KEY)
                last_s1_on = s1_on

            if last_s2_on is None or s2_on != last_s2_on:
                tap_key(SW2_ON_KEY if s2_on else SW2_OFF_KEY)
                last_s2_on = s2_on

    except KeyboardInterrupt:
        pass
    except serial.SerialException as e:
        print("ERRO serial durante leitura:", e, file=sys.stderr)
    finally:
        for name, _ in AXIS_ORDER:
            setattr(j.data, name, to_vjoy(128))
        j.set_button(BTN_S1, 0)
        j.set_button(BTN_S2, 0)
        j.update()
        try:
            ser.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()
