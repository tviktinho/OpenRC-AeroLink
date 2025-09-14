import json
import os
import sys
import threading
import time
from typing import Dict, Any

import PySimpleGUI as sg

from bridge_core import BridgeWorker, list_serial_ports


APP_NAME = "OpenRC-AeroLink"
CFG_FILE = "config.json"


def get_config_path() -> str:
    base = os.getenv("APPDATA") or os.path.expanduser("~")
    app_dir = os.path.join(base, APP_NAME)
    os.makedirs(app_dir, exist_ok=True)
    return os.path.join(app_dir, CFG_FILE)


def load_config() -> Dict[str, Any]:
    path = get_config_path()
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {
        "com_port": "",
        "baud": 115200,
        "vjoy_device_id": 1,
        "sw1_key": "g",
        "sw2_key": "r",
        "on_threshold": 10,
        "deadzone_255": 1,
        "smooth_n": 0,
        "invert": [False] * 8,
    }


def save_config(cfg: Dict[str, Any]) -> None:
    path = get_config_path()
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
    except Exception:
        pass


def run_gui() -> None:
    sg.theme("SystemDefault")

    cfg = load_config()

    ports = list(list_serial_ports())
    if cfg.get("com_port") and cfg["com_port"] not in ports:
        ports = [cfg["com_port"]] + ports

    layout = [
        [sg.Text("Porta COM:"), sg.Combo(ports, key="-PORT-", default_value=cfg.get("com_port", ""), size=(20, 1)),
         sg.Button("Atualizar", key="-REFRESH-")],
        [sg.Text("Switch 1 (tecla):"), sg.Input(cfg.get("sw1_key", "g"), key="-SW1-", size=(10, 1)),
         sg.Text("Switch 2 (tecla):"), sg.Input(cfg.get("sw2_key", "r"), key="-SW2-", size=(10, 1))],
        [sg.Button("Iniciar", key="-START-", button_color=("white", "green")),
         sg.Button("Parar", key="-STOP-", disabled=True),
         sg.Button("Salvar", key="-SAVE-"),
         sg.Button("Sair", key="-EXIT-")],
        [sg.Multiline("", key="-LOG-", size=(80, 20), autoscroll=True, reroute_stdout=True, reroute_stderr=True, write_only=True)],
    ]

    window = sg.Window(f"{APP_NAME} Bridge", layout, finalize=True)

    worker: "BridgeWorker | None"
    try:
        # Python <3.10 compatibility fallback
        worker = None  # type: ignore
    except Exception:
        worker = None

    def log(msg: str) -> None:
        try:
            window["-LOG-"].print(msg)
        except Exception:
            pass

    def set_running_state(running: bool) -> None:
        window["-START-"].update(disabled=running)
        window["-STOP-"].update(disabled=not running)
        window["-PORT-"].update(disabled=running)
        window["-SW1-"].update(disabled=running)
        window["-SW2-"].update(disabled=running)

    while True:
        event, values = window.read(timeout=200)
        if event in (sg.WIN_CLOSED, "-EXIT-"):
            if worker and worker.is_running:
                worker.stop()
            break

        if event == "-REFRESH-":
            window["-PORT-"].update(values=list_serial_ports())

        if event == "-SAVE-":
            cfg.update({
                "com_port": values.get("-PORT-", ""),
                "sw1_key": values.get("-SW1-", "g"),
                "sw2_key": values.get("-SW2-", "r"),
            })
            save_config(cfg)
            log("Configurações salvas.")

        if event == "-START-":
            com = values.get("-PORT-", "")
            if not com:
                log("[AVISO] Selecione uma porta COM.")
                continue
            sw1 = (values.get("-SW1-", "g") or "g").strip()
            sw2 = (values.get("-SW2-", "r") or "r").strip()

            cfg.update({
                "com_port": com,
                "sw1_key": sw1,
                "sw2_key": sw2,
            })
            save_config(cfg)

            # Create and start worker
            worker = BridgeWorker(
                com_port=com,
                baud=cfg.get("baud", 115200),
                vjoy_device_id=cfg.get("vjoy_device_id", 1),
                sw1_key=sw1,
                sw2_key=sw2,
                on_threshold=cfg.get("on_threshold", 10),
                deadzone_255=cfg.get("deadzone_255", 1),
                smooth_n=cfg.get("smooth_n", 0),
                invert=cfg.get("invert", [False] * 8),
                log=log,
            )
            set_running_state(True)
            worker.start()
            log("Execução iniciada.")

        # Poll worker status to auto-reset UI if it stops
        if event == "-STOP-":
            if worker:
                worker.stop()
                log("Execução parada.")
            set_running_state(False)

        if worker and not worker.is_running and window["-STOP-"].Disabled is False:
            # Worker stopped on its own
            set_running_state(False)

    window.close()


if __name__ == "__main__":
    run_gui()
