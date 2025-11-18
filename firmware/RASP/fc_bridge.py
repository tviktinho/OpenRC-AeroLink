import serial
import serial.tools.list_ports
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import time

# --- Configuração ---
BAUD_RATE = 115200
ESP32_PORT = None 

# --- Encontrar a porta serial correta ---
print("Procurando pela porta serial do ESP32...")
# Prioridade para ttyAMA0 (Hardware UART estável)
possible_ports = ['/dev/ttyAMA0', '/dev/ttyS0', '/dev/serial0']

for port in possible_ports:
    try:
        # Tenta abrir brevemente para ver se existe
        s = serial.Serial(port)
        s.close()
        ESP32_PORT = port
        print(f"Porta UART de hardware encontrada em: {ESP32_PORT}")
        break
    except (serial.SerialException, FileNotFoundError):
        pass

if not ESP32_PORT:
    # Fallback para USB se estiver usando adaptador
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if "CP210x" in p.description or "CH340" in p.description:
            ESP32_PORT = p.device
            break

if not ESP32_PORT:
    print("ERRO FATAL: Não foi possível encontrar o ESP32.")
    exit()

# --- Inicializar Serial ---
try:
    ser = serial.Serial(ESP32_PORT, BAUD_RATE, timeout=1.0)
    print(f"Conectado ao ESP32 em {ESP32_PORT} a {BAUD_RATE} baud.")
except serial.SerialException as e:
    print(f"ERRO: Falha ao abrir porta serial: {e}")
    exit()

app = Flask(__name__)
CORS(app) 

# --- Função Auxiliar de Comunicação ---
def send_command_to_esp32(json_command):
    try:
        # Limpa buffer de entrada antes de enviar
        ser.reset_input_buffer()
        
        # Envia comando
        ser.write(json_command.encode('utf-8') + b'\n')
        
        # Lê resposta
        while True:
            response_line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not response_line:
                break # Timeout
            
            # Filtra apenas respostas JSON válidas
            if "status" in response_line and ("ok" in response_line or "data" in response_line or "info" in response_line):
                return response_line

        return '{"status": "error", "msg": "Nenhuma resposta do ESP32 (timeout)"}'

    except Exception as e:
        return f'{{"status": "error", "msg": "Erro Serial: {str(e)}"}}'

# --- Rotas da API ---

@app.route('/api/set', methods=['POST'])
def handle_set():
    data = request.json
    cmd = f'{{"cmd":"set", "param":"{data["param"]}", "value":{data["value"]}}}'
    print(f"RPi -> ESP32: {cmd}")
    res = send_command_to_esp32(cmd)
    print(f"ESP32 -> RPi: {res}")
    return res

@app.route('/api/get', methods=['POST'])
def handle_get():
    data = request.json
    cmd = f'{{"cmd":"get", "param":"{data["param"]}"}}'
    print(f"RPi -> ESP32: {cmd}")
    res = send_command_to_esp32(cmd)
    print(f"ESP32 -> RPi: {res}")
    return res

@app.route('/api/action', methods=['POST'])
def handle_action():
    data = request.json
    cmd = f'{{"cmd":"action", "name":"{data["name"]}"}}'
    print(f"RPi -> ESP32: {cmd}")
    res = send_command_to_esp32(cmd)
    print(f"ESP32 -> RPi: {res}")
    return res

@app.route('/')
def index():
    return send_from_directory('.', 'configurador.html')

if __name__ == '__main__':
    print("Iniciando Servidor Web da Ponte FC...")
    app.run(host='0.0.0.0', port=5000)