import socket
import threading
import tkinter as tk
from tkinter import ttk, messagebox


class MetroAdminApp:
    def __init__(self, master: tk.Tk):
        self.master = master
        self.master.title("Metro Admin - Python")

        self.host_var = tk.StringVar(value="98.84.165.222")
        self.port_var = tk.StringVar(value="8080")

        self.user_var = tk.StringVar(value="admin")
        self.pass_var = tk.StringVar(value="1234")
        self.token_var = tk.StringVar(value="-")

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

        auth_frame = ttk.LabelFrame(self.master, text="Autenticación")
        auth_frame.pack(fill=tk.X, padx=10, pady=5)
        ttk.Label(auth_frame, text="Usuario:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Entry(auth_frame, textvariable=self.user_var, width=16).grid(row=0, column=1, padx=5, pady=5)
        ttk.Label(auth_frame, text="Clave:").grid(row=0, column=2, sticky=tk.W, padx=5, pady=5)
        ttk.Entry(auth_frame, textvariable=self.pass_var, show="*", width=16).grid(row=0, column=3, padx=5, pady=5)
        ttk.Button(auth_frame, text="Login", command=self.login).grid(row=0, column=4, padx=5, pady=5)
        ttk.Label(auth_frame, text="Token:").grid(row=1, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Label(auth_frame, textvariable=self.token_var).grid(row=1, column=1, columnspan=4, sticky=tk.W, padx=5, pady=5)

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

        cmd_frame = ttk.LabelFrame(self.master, text="Comandos")
        cmd_frame.pack(fill=tk.X, padx=10, pady=5)
        ttk.Button(cmd_frame, text="SPEED UP", command=lambda: self.send_command("SPEED UP")).grid(row=0, column=0, padx=5, pady=5)
        ttk.Button(cmd_frame, text="SLOW DOWN", command=lambda: self.send_command("SLOW DOWN")).grid(row=0, column=1, padx=5, pady=5)
        ttk.Button(cmd_frame, text="STOPNOW", command=lambda: self.send_command("STOPNOW")).grid(row=0, column=2, padx=5, pady=5)
        ttk.Button(cmd_frame, text="STARTNOW", command=lambda: self.send_command("STARTNOW")).grid(row=0, column=3, padx=5, pady=5)

    def connect(self) -> None:
        if self.running:
            return
        try:
            host = self.host_var.get().strip()
            port = int(self.port_var.get().strip())
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

    def login(self) -> None:
        if not self.sock:
            messagebox.showwarning("Conexión", "Conéctate primero al servidor")
            return
        user = self.user_var.get().strip()
        password = self.pass_var.get().strip()
        payload = f"USER={user};PASS={password}"
        msg = (
            "TYPE: LOGIN\n"
            "TOKEN: NULL\n"
            f"PAYLOAD_LENGTH: {len(payload)}\n"
            "PAYLOAD:\n"
            f"{payload}"
        )
        try:
            self.sock.sendall(msg.encode())
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def send_command(self, command: str) -> None:
        token = self.token_var.get().strip()
        if token in ("-", "", "NULL"):
            messagebox.showwarning("Autenticación", "Realiza login para obtener TOKEN")
            return
        payload = command
        msg = (
            "TYPE: COMMAND\n"
            f"TOKEN: {token}\n"
            f"PAYLOAD_LENGTH: {len(payload)}\n"
            "PAYLOAD:\n"
            f"{payload}"
        )
        try:
            self.sock.sendall(msg.encode())
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def _recv_loop(self) -> None:
        buffer = b""
        while self.running:
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                buffer += data
                # Procesar por bloques. Buscamos inicio de payload.
                while b"PAYLOAD:\n" in buffer:
                    header, _, rest = buffer.partition(b"PAYLOAD:\n")
                    header_text = header.decode(errors="ignore")
                    # Capturar TOKEN de respuesta de LOGIN si viene en header
                    token_line = next((line for line in header_text.splitlines() if line.startswith("TOKEN:")), None)
                    if token_line and self.token_var.get() in ("-", "", "NULL"):
                        token_value = token_line.split(":", 1)[1].strip()
                        if token_value and token_value != "NULL":
                            self.master.after(0, lambda v=token_value: self.token_var.set(v))
                    payload_text = rest.decode(errors="ignore")
                    self._process_payload(payload_text)
                    buffer = b""  # limpiar
            except Exception:
                break

        self.master.after(0, lambda: messagebox.showinfo("Conexión", "Desconectado del servidor"))
        self.disconnect()

    def _process_payload(self, payload_text: str) -> None:
        # TELEMETRY u OK/ERROR
        if payload_text.strip() == "OK":
            return
        if payload_text.startswith("ERROR"):
            self.master.after(0, lambda: messagebox.showwarning("Servidor", payload_text))
            return

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
    app = MetroAdminApp(root)
    root.protocol("WM_DELETE_WINDOW", lambda: (app.disconnect(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()


