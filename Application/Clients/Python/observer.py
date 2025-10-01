import socket
import threading
import tkinter as tk
from tkinter import ttk, messagebox


class TelemetryObserverApp:
    def __init__(self, master: tk.Tk):
        self.master = master
        self.master.title("Metro Observer - Python")

        self.host_var = tk.StringVar(value="")
        self.port_var = tk.StringVar(value="")

        self.station_var = tk.StringVar(value="-")
        self.speed_var = tk.StringVar(value="-")
        self.battery_var = tk.StringVar(value="-")
        self.direction_var = tk.StringVar(value="-")

        self.sock = None
        self.recv_thread = None
        self.running = False

        self._build_ui()

    def _build_ui(self) -> None:
        conn_frame = ttk.LabelFrame(self.master, text="Conexión")
        conn_frame.pack(fill=tk.X, padx=10, pady=10)

        ttk.Label(conn_frame, text="Host:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Entry(conn_frame, textvariable=self.host_var, width=18).grid(row=0, column=1, padx=5, pady=5)
        ttk.Label(conn_frame, text="Puerto:").grid(row=0, column=2, sticky=tk.W, padx=5, pady=5)
        ttk.Entry(conn_frame, textvariable=self.port_var, width=8).grid(row=0, column=3, padx=5, pady=5)
        ttk.Button(conn_frame, text="Conectar", command=self.connect).grid(row=0, column=4, padx=5, pady=5)
        ttk.Button(conn_frame, text="Desconectar", command=self.disconnect).grid(row=0, column=5, padx=5, pady=5)

        telem_frame = ttk.LabelFrame(self.master, text="Telemetría")
        telem_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        ttk.Label(telem_frame, text="Estación:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Label(telem_frame, textvariable=self.station_var).grid(row=0, column=1, sticky=tk.W, padx=5, pady=5)

        ttk.Label(telem_frame, text="Velocidad:").grid(row=1, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Label(telem_frame, textvariable=self.speed_var).grid(row=1, column=1, sticky=tk.W, padx=5, pady=5)

        ttk.Label(telem_frame, text="Batería:").grid(row=2, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Label(telem_frame, textvariable=self.battery_var).grid(row=2, column=1, sticky=tk.W, padx=5, pady=5)

        ttk.Label(telem_frame, text="Dirección:").grid(row=3, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Label(telem_frame, textvariable=self.direction_var).grid(row=3, column=1, sticky=tk.W, padx=5, pady=5)

    def connect(self) -> None:
        if self.running:
            return
        try:
            host = self.host_var.get().strip()
            print(f"Connecting to host: {host}")  # Debug print
            if not host:
                messagebox.showerror("Error", "Por favor ingrese una dirección IP")
                return
            port = int(self.port_var.get().strip())
            print(f"Connecting to port: {port}")
            self.sock = socket.create_connection((host, port), timeout=5)
            # Evitar desconexión por timeout mientras esperamos telemetría (10s)
            self.sock.settimeout(None)
        except Exception as e:
            messagebox.showerror("Error de conexión", str(e))
            return

        self.running = True
        self.recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self.recv_thread.start()

    def disconnect(self) -> None:
        self.running = False
        try:
            if self.sock:
                self.sock.close()
        finally:
            self.sock = None

    def _recv_loop(self) -> None:
        buffer = b""
        while self.running:
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                buffer += data
                # Procesar mensajes por bloques; TELEMETRY llega completo normalmente
                while b"PAYLOAD:\n" in buffer:
                    header, _, rest = buffer.partition(b"PAYLOAD:\n")
                    # Buscar fin aparente de payload (siguiente TYPE o fin de buffer)
                    # Como el servidor no manda delimitador extra, tratamos todo lo disponible
                    payload = rest.decode(errors="ignore")
                    self._process_payload(payload)
                    buffer = b""  # limpiar para el siguiente mensaje
            except Exception:
                break

        # Conexión cerrada
        self.master.after(0, lambda: messagebox.showinfo("Conexión", "Desconectado del servidor"))
        self.disconnect()

    def _process_payload(self, payload_text: str) -> None:
        # Extraer claves esperadas
        station = self._extract_value(payload_text, "STATION")
        speed = self._extract_value(payload_text, "SPEED")
        battery = self._extract_value(payload_text, "BATTERY")
        direction = self._extract_value(payload_text, "DIRECTION")

        if station:
            self.master.after(0, lambda: self.station_var.set(station))
        if speed:
            self.master.after(0, lambda: self.speed_var.set(speed))
        if battery:
            self.master.after(0, lambda: self.battery_var.set(battery))
        if direction:
            self.master.after(0, lambda: self.direction_var.set(direction))

    @staticmethod
    def _extract_value(text: str, key: str) -> str:
        for line in text.splitlines():
            if line.startswith(key + "="):
                return line.split("=", 1)[1].strip()
        return ""


def main() -> None:
    root = tk.Tk()
    app = TelemetryObserverApp(root)
    root.protocol("WM_DELETE_WINDOW", lambda: (app.disconnect(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()


