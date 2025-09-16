import json
import os
import sys
from typing import Dict, Any

# Prefer the free community fork first, then fall back to PySimpleGUI
try:
    import FreeSimpleGUI as sg  # type: ignore
except Exception:
    import PySimpleGUI as sg  # type: ignore

# Compatibility: in some packaged environments elements live under
# PySimpleGUI.PySimpleGUI. If attributes like Text/theme aren't present
# on the top-level package, fall back to the submodule.
try:  # pragma: no cover
    _ = sg.Text  # type: ignore[attr-defined]
except Exception:  # noqa: BLE001
    try:
        import PySimpleGUI.PySimpleGUI as _psg  # type: ignore
        sg = _psg  # type: ignore
    except Exception:
        pass

try:
    import ctypes  # For Windows message box fallback when GUI base missing
except Exception:  # pragma: no cover
    ctypes = None  # type: ignore

from bridge_core import BridgeWorker, list_serial_ports


def resource_path(name: str) -> str:
    base = getattr(sys, "_MEIPASS", os.path.dirname(__file__))
    return os.path.join(base, name)


APP_NAME = "OpenRC-AeroLink"
CFG_FILE = "config.json"


def _fatal_popup(msg: str) -> None:
    try:
        if sys.platform.startswith("win") and ctypes is not None:
            MB_ICONERROR = 0x10
            ctypes.windll.user32.MessageBoxW(0, msg, APP_NAME, MB_ICONERROR)  # type: ignore[attr-defined]
        else:
            print(msg, file=sys.stderr)
    except Exception:
        pass
    sys.exit(1)


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
    # Use modern theme API if available; fall back for old PySimpleGUI
    try:
        if hasattr(sg, "theme"):
            sg.theme("SystemDefault")
        else:
            # Older PySimpleGUI used ChangeLookAndFeel
            sg.ChangeLookAndFeel("SystemDefault")  # type: ignore[attr-defined]
    except Exception:
        # If theming isn't available, continue with defaults
        pass

    # Validate required GUI elements exist (tkinter collected correctly)
    required_attrs = ("Text", "Button", "Window")
    if not all(hasattr(sg, a) for a in required_attrs):
        loc = getattr(sg, "__file__", "<unknown>")
        ver = getattr(sg, "version", "<unknown>")
        _fatal_popup(
            "Biblioteca de GUI incompleta no executável.\n"
            f"Arquivo: {loc}\nVersão: {ver}\n\n"
            "Reinstale as dependências e gere o .exe novamente.\n"
            "Dica: prefira FreeSimpleGUI (gratuito) ou inclua tkinter no build."
        )

    cfg = load_config()

    ports = list(list_serial_ports())
    if cfg.get("com_port") and cfg["com_port"] not in ports:
        ports = [cfg["com_port"]] + ports

    # Potentiometer bars (P0..P7)
    bars_col = []
    for i in range(8):
        bars_col.append([
            sg.Text(f"P{i}", size=(3, 1)),
            sg.ProgressBar(max_value=255, orientation="h", size=(30, 15), key=f"-PB-{i}-"),
            sg.Text("000", key=f"-PV-{i}-", size=(4, 1)),
        ])

    layout = [
        [sg.Text("Porta COM:"), sg.Combo(ports, key="-PORT-", default_value=cfg.get("com_port", ""), size=(20, 1), readonly=True),
         sg.Button("Atualizar", key="-REFRESH-")],
        [sg.Frame("Switches", [[
            sg.Checkbox("S1", key="-S1-LED-", disabled=True),
            sg.Checkbox("S2", key="-S2-LED-", disabled=True),
        ]])],
        [sg.Text("Switch 1 (tecla):"), sg.Input(cfg.get("sw1_key", "g"), key="-SW1-", size=(10, 1)),
         sg.Text("Switch 2 (tecla):"), sg.Input(cfg.get("sw2_key", "r"), key="-SW2-", size=(10, 1))],
        [sg.Button("Iniciar", key="-START-", button_color=("white", "green")),
         sg.Button("Parar", key="-STOP-", disabled=True),
         sg.Button("Salvar", key="-SAVE-"),
         sg.Button("Sair", key="-EXIT-")],
        [sg.Frame("Potenciômetros", [[sg.Column(bars_col)]], expand_x=True)],
        [sg.Multiline("", key="-LOG-", size=(80, 12), autoscroll=True, reroute_stdout=True, reroute_stderr=True, write_only=True, disabled=True)],
    ]

    window = sg.Window(
        f"{APP_NAME} Bridge",
        layout,
        finalize=True,
        icon=resource_path("icon.ico"),
    )

    worker: "BridgeWorker | None"
    try:
        # Python <3.10 compatibility fallback
        worker = None  # type: ignore
    except Exception:
        worker = None

    def log(msg: str) -> None:
        try:
            window["-LOG-"].update(disabled=False)
            window["-LOG-"].print(msg)
        except Exception:
            pass
        finally:
            try:
                window["-LOG-"].update(disabled=True)
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
            def post_data(payload: Dict[str, Any]) -> None:
                try:
                    window.write_event_value("-DATA-", payload)
                except Exception:
                    pass

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
                on_data=post_data,
            )
            set_running_state(True)
            worker.start()
            try:
                window.refresh()
            except Exception:
                pass
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

        # Live data updates from worker
        if event == "-DATA-":
            data = values.get("-DATA-", {}) or {}
            pots = data.get("pots") or []
            # Update switch indicators
            try:
                s1 = 1 if data.get("s1") else 0
                s2 = 1 if data.get("s2") else 0
                window["-S1-LED-"].update(value=bool(s1), text_color=("green" if s1 else "red"))
                window["-S2-LED-"].update(value=bool(s2), text_color=("green" if s2 else "red"))
            except Exception:
                pass
            if isinstance(pots, (list, tuple)) and len(pots) >= 8:
                for i in range(8):
                    try:
                        v = int(pots[i])
                        window[f"-PB-{i}-"].update(current_count=max(0, min(255, v)))
                        window[f"-PV-{i}-"].update(f"{v:03d}")
                    except Exception:
                        pass

    window.close()


if __name__ == "__main__":
    run_gui()
