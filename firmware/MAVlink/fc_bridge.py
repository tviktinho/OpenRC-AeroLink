#!/usr/bin/env python3
import serial
import time
import threading
import json
import sys
from pymavlink import mavutil

# Configurações
POSSIBLE_PORTS = ['/dev/ttyAMA0', '/dev/ttyS0', '/dev/ttyUSB0', 'COM3', 'COM4']
BAUD = 115200
MAV_UDP_OUT = 'udpout:127.0.0.1:14550'  # onde o MAVLink será emitido (QGroundControl / listeners)

# Protocol: frame: 0xAA 0x55 [p0..p7] [s1] [s2] [chk]
START0 = 0xAA
START1 = 0x55

# Função utilitária para encontrar porta serial
def find_serial_port():
    for p in POSSIBLE_PORTS:
        try:
            s = serial.Serial(p, BAUD, timeout=0.5)
            s.close()
            return p
        except Exception:
            continue
    return None


class RCtoMAVBridge:
    def __init__(self, serial_port=None, baud=BAUD, mav_uri=MAV_UDP_OUT, sysid=1, compid=200):
        self.serial_port = serial_port or find_serial_port()
        if not self.serial_port:
            raise RuntimeError('Serial port para receptor não encontrado. Ajuste POSSIBLE_PORTS.')
        print(f'Conectando receptor em: {self.serial_port} @ {baud}')
        self.ser = serial.Serial(self.serial_port, baud, timeout=0.1)
        print('Conectado serial.')

        print(f'Criando saída MAVLink em: {mav_uri}')
        self.mav = mavutil.mavlink_connection(mav_uri, source_system=sysid, source_component=compid)
        # manter sequência de heartbeat
        self.last_heartbeat = 0
        self.seq = 0
        self.running = True

    def start(self):
        t = threading.Thread(target=self._reader_loop, daemon=True)
        t.start()
        print('Bridge rodando. CTRL-C para sair.')
        try:
            while True:
                now = time.time()
                if now - self.last_heartbeat > 1.0:
                    # Envia heartbeat (1 Hz)
                    self.mav.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GENERIC,
                                                mavutil.mavlink.MAV_AUTOPILOT_GENERIC,
                                                0, 0, mavutil.mavlink.MAV_STATE_ACTIVE)
                    self.last_heartbeat = now
                time.sleep(0.1)
        except KeyboardInterrupt:
            print('\nEncerrando...')
            self.running = False
            self.ser.close()

    def _reader_loop(self):
        buf = bytearray()
        while self.running:
            try:
                data = self.ser.read(64)
                if not data:
                    time.sleep(0.01)
                    continue
                buf.extend(data)
                # procura por start bytes
                while True:
                    if len(buf) < 2:
                        break
                    try:
                        idx = buf.index(START0)
                    except ValueError:
                        buf.clear()
                        break
                    if idx + 1 >= len(buf):
                        # aguarda mais
                        break
                    if buf[idx+1] != START1:
                        # descarta até idx+1
                        buf.pop(0)
                        continue
                    # temos start at idx
                    if len(buf) < idx + 2 + 11:  # 10 payload + 1 chk
                        # aguarda mais bytes
                        break
                    frame = buf[idx+2:idx+2+11]
                    # remove até fim do frame
                    del buf[:idx+2+11]

                    payload = frame[:10]
                    chk = frame[10]
                    # checksum é XOR simples (como no NANO_RX)
                    x = 0
                    for b in payload:
                        x ^= b
                    if x != chk:
                        print('Checksum inválido, descartando frame')
                        continue
                    # Parse channels
                    ch = [payload[i] for i in range(8)]
                    s1 = payload[8]
                    s2 = payload[9]
                    # Converte byte 0..255 -> microseconds 1000..2000
                    ch_us = [1000 + (int(b) * 1000) // 255 for b in ch]
                    # Envia RC_CHANNELS_RAW (time_boot_ms, port, chan1..8, rssi)
                    tms = int(time.time() * 1000) & 0xFFFFFFFF
                    try:
                        self.mav.mav.rc_channels_raw_send(tms, 0,
                                                          ch_us[0], ch_us[1], ch_us[2], ch_us[3],
                                                          ch_us[4], ch_us[5], ch_us[6], ch_us[7],
                                                          0)
                        print(f'RC -> {ch_us} S1={s1} S2={s2}')
                    except Exception as e:
                        print('Erro ao enviar MAVLink RC:', e)
            except Exception as e:
                print('Erro serial:', e)
                time.sleep(0.5)


if __name__ == '__main__':
    port = None
    if len(sys.argv) >= 2:
        port = sys.argv[1]
    try:
        bridge = RCtoMAVBridge(serial_port=port)
        bridge.start()
    except Exception as e:
        print('Falha ao iniciar bridge:', e)
        sys.exit(1)
