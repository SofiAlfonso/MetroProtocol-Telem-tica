## Clientes MetroProtocol

### Requisitos
- Python 3.9+
- Java 11+

### Python

Ubicación: `Application/Clients/Python/`

Observer (solo telemetría):
```bash
python3 observer.py
```

Admin (login + comandos + telemetría):
```bash
python3 admin.py
```

En ambos, configura `Host` y `Puerto` y presiona "Conectar". En Admin, realiza `Login` para obtener `TOKEN` y habilitar envíos de `COMMAND`.

### Java

Ubicación: `Application/Clients/Java/src/`

Compilar y ejecutar Observer:
```bash
javac Application/Clients/Java/src/Observer.java && \
java -cp Application/Clients/Java/src Observer
```

Compilar y ejecutar Admin:
```bash
javac Application/Clients/Java/src/Admin.java && \
java -cp Application/Clients/Java/src Admin
```

Configura `Host` y `Puerto`. En Admin, realiza `Login` para obtener `TOKEN` antes de enviar comandos.

### Notas
- El servidor debe estar ejecutándose: `./MetroProtocol <port> <LogsFile>`
- Credenciales por defecto: `admin / 1234` (definidas en el servidor actual).
- El protocolo es textual. Los clientes parsean telemetría del `PAYLOAD`.


