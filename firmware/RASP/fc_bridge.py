import serial
import time
import pigpio
import smbus
import threading
import json
import math
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS

# === CONFIGURAÇÕES DE HARDWARE ===
ESP_BAUD = 115200
# Tenta portas na ordem de preferência
POSSIBLE_PORTS = ['/dev/ttyAMA0', '/dev/ttyS0', '/dev/ttyUSB0']

GPS_RX_PIN = 23
GPS_BAUD = 9600

BARO_ADDR = 0x77 # Endereço que achamos no i2cdetect
BARO_BUS = 1

# === ESTADO GLOBAL (Dados compartilhados entre as threads) ===
telemetry_data = {
    # Dados vindos do ESP32
    "roll": 0, "pitch": 0, "mode": "N/A", "throttle": 0, 
    # Dados vindos do RPi
    "lat": 0, "lon": 0, "sats": 0, "fix": False,
    "alt": 0, "temp": 0, "press": 0
}

# Semáforo para impedir que duas rotas usem a Serial ao mesmo tempo
serial_lock = threading.Lock()

app = Flask(__name__)
CORS(app)

# --- 1. INICIALIZAÇÃO DO ESP32 ---
ser_esp = None
print("--- INICIANDO SISTEMA ---")
for p in POSSIBLE_PORTS:
    try:
        s = serial.Serial(p, ESP_BAUD, timeout=1.0)
        s.close()
        ser_esp = serial.Serial(p, ESP_BAUD, timeout=1.0)
        print(f"✅ ESP32 conectado em: {p}")
        break
    except: pass

if not ser_esp: print("❌ AVISO: ESP32 não encontrado na UART.")


# --- 2. INICIALIZAÇÃO DO GPS (Pigpio) ---
pi = pigpio.pi()
gps_ready = False
if not pi.connected:
    print("❌ ERRO: 'pigpiod' não está rodando.")
else:
    try:
        pi.bb_serial_read_open(GPS_RX_PIN, GPS_BAUD, 8)
        print(f"✅ GPS monitorado no GPIO {GPS_RX_PIN}")
        gps_ready = True
    except: 
        print("⚠️ GPS: Porta já estava aberta (OK)")
        gps_ready = True

# --- 3. INICIALIZAÇÃO DO BARÔMETRO (BMP180) ---
class BMP180:
    def __init__(self):
        try:
            self.bus = smbus.SMBus(BARO_BUS)
            self.address = BARO_ADDR
            self.calib = {}
            self._load_calibration()
            print(f"✅ Barômetro BMP180 encontrado em 0x{BARO_ADDR:x}")
        except: 
            self.bus = None
            print("❌ Barômetro não encontrado (I2C)")

    def _read_word(self, reg):
        if not self.bus: return 0
        try:
            high = self.bus.read_byte_data(self.address, reg)
            low = self.bus.read_byte_data(self.address, reg + 1)
            val = (high << 8) + low
            return val - 65536 if val >= 32768 else val
        except: return 0

    def _load_calibration(self):
        # Carrega coeficientes (Simplificado para não travar se falhar)
        self.calib['AC1'] = self._read_word(0xAA); self.calib['AC2'] = self._read_word(0xAC)
        self.calib['AC3'] = self._read_word(0xAE); self.calib['AC4'] = self._read_word(0xB0) # Unsigned tratado abaixo
        self.calib['AC5'] = self._read_word(0xB2); self.calib['AC6'] = self._read_word(0xB4)
        self.calib['B1']  = self._read_word(0xB6); self.calib['B2']  = self._read_word(0xB8)
        self.calib['MB']  = self._read_word(0xBA); self.calib['MC']  = self._read_word(0xBC); self.calib['MD'] = self._read_word(0xBE)

    def read(self):
        if not self.bus: return 0, 0
        try:
            # Leitura simplificada para este exemplo (Implementar lógica completa matemática se necessário precisão cm)
            # Apenas verificando se comunica
            self.bus.write_byte_data(self.address, 0xF4, 0x2E); time.sleep(0.005)
            return 25.0, 1013.25 # Retorna dummy se funcionar leitura I2C basica
        except: return 0, 0

baro = BMP180()


# --- THREADS DE LEITURA (Background) ---

def thread_gps_loop():
    if not gps_ready: return
    while True:
        try:
            (count, data) = pi.bb_serial_read(GPS_RX_PIN)
            if count > 0:
                text = data.decode('latin-1', errors='ignore')
                lines = text.split('\n')
                for line in lines:
                    if "GNGGA" in line or "GPGGA" in line:
                        p = line.split(',')
                        if len(p) > 7 and p[6] != '0':
                            telemetry_data['fix'] = True
                            telemetry_data['sats'] = int(p[7])
                            # Conversão NMEA para Decimal
                            if p[2]: 
                                lat = float(p[2])
                                telemetry_data['lat'] = int(lat/100) + (lat%100)/60
                                if p[3] == 'S': telemetry_data['lat'] *= -1
                            if p[4]:
                                lon = float(p[4])
                                telemetry_data['lon'] = int(lon/100) + (lon%100)/60
                                if p[5] == 'W': telemetry_data['lon'] *= -1
                        else:
                            telemetry_data['fix'] = False
        except: pass
        time.sleep(0.2) # 5Hz update

def thread_baro_loop():
    # Lógica simples de altitude baseada em pressão
    while True:
        # t, p = baro.read()
        # Aqui entraria a matemática completa do BMP180
        # alt = 44330 * (1.0 - math.pow(p / 1013.25, 0.1903))
        # telemetry_data['alt'] = alt
        time.sleep(1)

# Inicia Threads
t_gps = threading.Thread(target=thread_gps_loop); t_gps.daemon=True; t_gps.start()
# t_baro = threading.Thread(target=thread_baro_loop); t_baro.daemon=True; t_baro.start()


# --- COMUNICAÇÃO SERIAL SEGURA ---
def talk_to_esp(cmd_string):
    if not ser_esp: return '{"status":"error", "msg":"Serial Off"}'
    
    # O Lock garante que ninguém interrompa uma conversa no meio
    with serial_lock:
        try:
            ser_esp.reset_input_buffer()
            ser_esp.write((cmd_string + '\n').encode())
            line = ser_esp.readline().decode('utf-8', errors='ignore').strip()
            if not line: return '{"status":"error", "msg":"Timeout"}'
            
            # Verifica se é JSON válido (começa com { e termina com })
            if not (line.startswith('{') and line.endswith('}')):
                print(f"LIXO: {line}")
                return '{"status":"error", "msg":"Dados corrompidos"}'
                
            return line
        except Exception as e:
            return f'{{"status":"error", "msg":"{str(e)}"}}'


# --- ROTAS API ---

@app.route('/api/get', methods=['POST'])
def api_get():
    data = request.json
    
    # Se pedir telemetria, busca no ESP e mistura com GPS local
    if data.get('param') == 'telemetry':
        resp = talk_to_esp('{"cmd":"telemetry"}')
        if "{" in resp:
            try:
                d = json.loads(resp)
                telemetry_data['roll'] = d.get('r', 0)
                telemetry_data['pitch'] = d.get('p', 0)
                telemetry_data['mode'] = d.get('m', 'N/A')
                telemetry_data['throttle'] = d.get('t', 0)
            except: pass
        return jsonify(telemetry_data)

    # Outros gets
    cmd = json.dumps({"cmd":"get", "param":data['param']})
    return talk_to_esp(cmd)

@app.route('/api/set', methods=['POST'])
def api_set():
    d = request.json
    # Suporte para envio agrupado de RC (rc_all)
    if d.get('param') == 'rc_all':
        # Monta o JSON exatamente como o ESP32 espera
        cmd_dict = {"cmd": "set", "param": "rc_all"}
        # Campos opcionais
        if 'deadzone' in d:
            cmd_dict['deadzone'] = d['deadzone']
        if 'servo_trim_l' in d:
            cmd_dict['servo_trim_l'] = d['servo_trim_l']
        if 'servo_trim_r' in d:
            cmd_dict['servo_trim_r'] = d['servo_trim_r']
        if 'trim' in d and isinstance(d['trim'], list):
            cmd_dict['trim'] = d['trim']
        cmd = json.dumps(cmd_dict)
        return talk_to_esp(cmd)

    # comportamento antigo para parâmetros simples (param + value)
    cmd = json.dumps({"cmd":"set", "param":d['param'], "value":d.get('value')})
    return talk_to_esp(cmd)

@app.route('/api/action', methods=['POST'])
def api_action():
    d = request.json
    cmd = json.dumps({"cmd":"action", "name":d['name']})
    return talk_to_esp(cmd)

@app.route('/')
def index(): return send_from_directory('.', 'configurador.html')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)