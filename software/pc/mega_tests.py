import serial, sys
import pyvjoy
import keyboard

# ===== CONFIG =====
SERIAL_PORT = "COM10"   # <- troque para a COM do NANO_TX (ex.: "COM7")
BAUD = 115200           # NANO_TX usa 38400
TIMEOUT_S = 1.0

VJOY_DEVICE_ID = 1
BTN_S1 = 9
BTN_S2 = 10

# 8 eixos do vJoy (ajuste conforme configurou no vJoy Config)
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
DEADZONE_255 = 1

def to_vjoy(v, invert=False):
    v = 0 if v < 0 else 255 if v > 255 else v
    if abs(v - 127.5) <= DEADZONE_255: v = 128
    if invert: v = 255 - v
    out = int(round((v / 255.0) * 32767.0))
    return 1 if out < 1 else 32767 if out > 32767 else out

# crc8 polinômio 0x8C (igual ao Arduino)
def crc8(data: bytes) -> int:
    c = 0x00
    for x in data:
        b = x
        for _ in range(8):
            mix = (c ^ b) & 1
            c >>= 1
            if mix:
                c ^= 0x8C
            b >>= 1
    return c & 0xFF

def read_payload_from_tx(port):
    # Estado simples para achar 0xAA 0x55
    b = port.read(1)
    if not b or b[0] != 0xAA:
        return None
    b = port.read(1)
    if len(b) != 1 or b[0] != 0x55:
        return None

    # Lê length
    b = port.read(1)
    if len(b) != 1:
        return None
    length = b[0]
    if length < 1 or length > 32:
        return None

    # Lê payload + CRC
    buf = port.read(length + 1)
    if len(buf) != length + 1:
        return None

    pay = buf[:-1]
    crc = buf[-1]
    if crc8(pay) != crc:
        return None

    # Esperado: length = 11 (8 ch + sw + rfu0 + rfu1)
    if length != 11:
        return None

    ch = list(pay[0:8])
    sw = pay[8]
    # pay[9], pay[10] = rfu0, rfu1

    # Bits: sw bit0 = S1, bit1 = S2, bit2 = CAL
    s1 = 1 if (sw & 0x01) else 0
    s2 = 1 if (sw & 0x02) else 0
    cal = 1 if (sw & 0x04) else 0
    return ch, s1, s2, cal

def tap_key(name):
    try:
        keyboard.press_and_release(name)
    except Exception:
        pass

def main():
    print(f">> vJoy#{VJOY_DEVICE_ID}, Serial {SERIAL_PORT}@{BAUD}")
    try:
        j = pyvjoy.VJoyDevice(VJOY_DEVICE_ID)
        print(">> vJoy OK.")
    except Exception as e:
        print("[ERRO] vJoy:", e, file=sys.stderr)
        return
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD, timeout=TIMEOUT_S)
        print(">> Serial OK.")
    except Exception as e:
        print("[ERRO] Serial:", e, file=sys.stderr)
        return

    last_s1 = None
    last_s2 = None

    try:
        while True:
            f = read_payload_from_tx(ser)
            if f is None:
                continue

            pots, s1, s2, cal = f

            # Eixos
            for i, (name, src) in enumerate(AXIS_ORDER):
                setattr(j.data, name, to_vjoy(pots[src], invert=INVERT[i]))
            j.update()

            # Botões (HOLD)
            j.set_button(BTN_S1, 1 if s1 else 0)
            j.set_button(BTN_S2, 1 if s2 else 0)
            j.update()

            # (Opcional) teclas em borda
            if last_s1 is None or s1 != last_s1:
                tap_key('g')   # mesma tecla dos seus testes
                last_s1 = s1
            if last_s2 is None or s2 != last_s2:
                tap_key('f')
                last_s2 = s2

    except KeyboardInterrupt:
        print("\n>> Encerrando…")
    finally:
        try: ser.close()
        except: pass

if __name__ == "__main__":
    # Troque COMx antes de rodar
    if "COMx" in SERIAL_PORT:
        print(">> Ajuste SERIAL_PORT para a COM do NANO_TX (ex.: COM7).")
    main()
