import sys
import time
from collections import deque
from threading import Event, Thread
from typing import Callable, Optional, Sequence, Dict, Any

try:
    import serial
    from serial.tools import list_ports
except Exception as e:  # pragma: no cover
    serial = None
    list_ports = None

try:
    import pyvjoy
except Exception as e:  # pragma: no cover
    pyvjoy = None

try:
    import keyboard
except Exception as e:  # pragma: no cover
    keyboard = None


def list_serial_ports() -> Sequence[str]:
    """Return a list of COM port device names (e.g., 'COM3')."""
    if list_ports is None:
        return []
    return [p.device for p in list_ports.comports()]


def to_vjoy(val_255: int, deadzone_255: int = 1, invert: bool = False) -> int:
    if val_255 < 0:
        val_255 = 0
    if val_255 > 255:
        val_255 = 255
    if abs(val_255 - 127.5) <= deadzone_255:
        val_255 = 128
    if invert:
        val_255 = 255 - val_255
    out = int(round((val_255 / 255.0) * 32767.0))
    return max(1, min(32767, out))


class BridgeWorker:
    """
    Background worker that reads serial data, feeds vJoy axes and emits key presses on switch edges.
    """

    AXIS_ORDER = [
        ("wAxisX", 0),
        ("wAxisY", 1),
        ("wAxisZ", 2),
        ("wAxisXRot", 3),
        ("wAxisYRot", 4),
        ("wAxisZRot", 5),
        ("wSlider", 6),
        ("wDial", 7),
    ]

    def __init__(
        self,
        com_port: str,
        baud: int = 115200,
        timeout_s: float = 1.0,
        vjoy_device_id: int = 1,
        sw1_key: str = "g",
        sw2_key: str = "r",
        on_threshold: int = 10,
        deadzone_255: int = 1,
        smooth_n: int = 0,
        invert: Optional[Sequence[bool]] = None,
        log: Optional[Callable[[str], None]] = None,
        on_data: Optional[Callable[[Dict[str, Any]], None]] = None,
    ) -> None:
        self.com_port = com_port
        self.baud = baud
        self.timeout_s = timeout_s
        self.vjoy_device_id = vjoy_device_id
        self.sw1_key = sw1_key
        self.sw2_key = sw2_key
        self.on_threshold = on_threshold
        self.deadzone_255 = deadzone_255
        self.smooth_n = max(0, smooth_n)
        self.invert = list(invert) if invert is not None else [False] * 8
        self.log = log or (lambda msg: None)
        self.on_data = on_data

        self._thread: Optional[Thread] = None
        self._stop = Event()
        self._running = Event()

    @property
    def is_running(self) -> bool:
        return self._running.is_set()

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = Thread(target=self._run, name="BridgeWorker", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.5)

    # Internal
    def _tap_key(self, key_name: str) -> None:
        if not key_name:
            return
        if keyboard is None:
            self.log(f"[WARN] Módulo 'keyboard' ausente; ignorando tecla '{key_name}'.")
            return
        try:
            keyboard.press_and_release(key_name)
        except Exception as e:
            self.log(f"[WARN] Falha ao enviar tecla '{key_name}': {e}")

    def _run(self) -> None:
        if pyvjoy is None:
            self.log("[ERRO] pyvjoy não disponível. Instale o driver vJoy e a lib pyvjoy.")
            return
        if serial is None:
            self.log("[ERRO] pyserial não disponível.")
            return

        try:
            j = pyvjoy.VJoyDevice(self.vjoy_device_id)
        except Exception as e:
            self.log(f"[ERRO] Não foi possível abrir vJoy device {self.vjoy_device_id}: {e}")
            return

        try:
            ser = serial.Serial(self.com_port, self.baud, timeout=self.timeout_s)
        except Exception as e:
            self.log(f"[ERRO] Falha ao abrir porta {self.com_port}: {e}")
            return

        self._running.set()
        self.log(
            f"Conectado em {self.com_port} @ {self.baud}. vJoy={self.vjoy_device_id}. Sw1={self.sw1_key} Sw2={self.sw2_key}"
        )

        smooth = [deque(maxlen=max(1, self.smooth_n)) for _ in range(8)]
        last_ok = time.time()
        last_warn = 0.0
        dbg_bad_lines = 0
        last_s1_on: Optional[int] = None
        last_s2_on: Optional[int] = None
        last_emit = 0.0
        emit_interval = 0.03  # ~33 Hz UI updates

        try:
            while not self._stop.is_set():
                line_bytes = ser.readline()
                now = time.time()
                if not line_bytes:
                    if now - last_ok > 1.0:
                        # Neutralize axes and release buttons on timeout
                        for name, _ in self.AXIS_ORDER:
                            setattr(j.data, name, to_vjoy(128, self.deadzone_255, False))
                        j.set_button(1, 0)
                        j.set_button(2, 0)
                        j.update()
                        # Log a gentle warning at most every 2s
                        if now - last_warn > 2.0:
                            self.log("[WARN] Sem dados do MEGA (timeout). Verifique conexões e porta COM.")
                            last_warn = now
                    continue

                try:
                    line = line_bytes.decode(errors="ignore").strip()
                except Exception:
                    continue

                parts = line.split(",")
                if len(parts) != 10:
                    if dbg_bad_lines < 3 and line:
                        self.log(f"[INFO] Linha ignorada: '{line}'")
                        dbg_bad_lines += 1
                    continue

                try:
                    vals = [int(x) for x in parts]
                except ValueError:
                    continue

                pots = vals[:8]
                s1_raw, s2_raw = vals[8], vals[9]
                s1_on = 1 if s1_raw >= self.on_threshold else 0
                s2_on = 1 if s2_raw >= self.on_threshold else 0

                # Axes
                for i, (name, src_idx) in enumerate(self.AXIS_ORDER):
                    v = pots[src_idx]
                    smooth[i].append(v)
                    if self.smooth_n > 0:
                        v = sum(smooth[i]) / len(smooth[i])
                    setattr(
                        j.data,
                        name,
                        to_vjoy(int(round(v)), deadzone_255=self.deadzone_255, invert=self.invert[i]),
                    )

                # Buttons (hold)
                btn_s1 = 1 if s1_on else 0
                btn_s2 = 1 if s2_on else 0
                # Default to buttons 1 and 2 for compatibility
                j.set_button(1, btn_s1)
                j.set_button(2, btn_s2)

                j.update()
                last_ok = now

                # Edge-triggered key taps
                if last_s1_on is None or s1_on != last_s1_on:
                    self._tap_key(self.sw1_key)
                    last_s1_on = s1_on

                if last_s2_on is None or s2_on != last_s2_on:
                    self._tap_key(self.sw2_key)
                    last_s2_on = s2_on

                # Emit live data for GUI (throttled)
                if self.on_data is not None and (now - last_emit) >= emit_interval:
                    try:
                        self.on_data({
                            "pots": pots,
                            "s1": s1_on,
                            "s2": s2_on,
                        })
                    except Exception:
                        pass
                    last_emit = now

        except Exception as e:
            self.log(f"[ERRO] Execução interrompida: {e}")
        finally:
            # Neutralize and cleanup
            try:
                for name, _ in self.AXIS_ORDER:
                    setattr(j.data, name, to_vjoy(128, self.deadzone_255, False))
                j.set_button(1, 0)
                j.set_button(2, 0)
                j.update()
            except Exception:
                pass
            try:
                ser.close()
            except Exception:
                pass
            self._running.clear()
            self.log("Conexão finalizada.")
