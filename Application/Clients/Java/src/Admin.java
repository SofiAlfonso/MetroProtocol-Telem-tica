import javax.swing.*;
import java.awt.*;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;

public class Admin extends JFrame {
    private final JTextField hostField = new JTextField("127.0.0.1", 12);
    private final JTextField portField = new JTextField("8080", 6);
    private final JButton connectButton = new JButton("Conectar");
    private final JButton disconnectButton = new JButton("Desconectar");

    private final JTextField userField = new JTextField("admin", 10);
    private final JPasswordField passField = new JPasswordField("1234", 10);
    private final JButton loginButton = new JButton("Login");
    private final JLabel tokenValue = new JLabel("-");

    private final JLabel stationValue = new JLabel("-");
    private final JLabel speedValue = new JLabel("-");
    private final JLabel batteryValue = new JLabel("-");
    private final JLabel directionValue = new JLabel("-");

    private final JButton speedUpBtn = new JButton("SPEED UP");
    private final JButton slowDownBtn = new JButton("SLOW DOWN");
    private final JButton stopNowBtn = new JButton("STOPNOW");
    private final JButton startNowBtn = new JButton("STARTNOW");

    private volatile boolean running = false;
    private Socket socket;
    private Thread recvThread;

    public Admin() {
        super("Metro Admin - Java");
        setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);
        setLayout(new BorderLayout());

        JPanel conn = new JPanel(new FlowLayout(FlowLayout.LEFT));
        conn.setBorder(BorderFactory.createTitledBorder("Conexión"));
        conn.add(new JLabel("Host:")); conn.add(hostField);
        conn.add(new JLabel("Puerto:")); conn.add(portField);
        conn.add(connectButton); conn.add(disconnectButton);
        add(conn, BorderLayout.NORTH);

        JPanel auth = new JPanel(new FlowLayout(FlowLayout.LEFT));
        auth.setBorder(BorderFactory.createTitledBorder("Autenticación"));
        auth.add(new JLabel("Usuario:")); auth.add(userField);
        auth.add(new JLabel("Clave:")); auth.add(passField);
        auth.add(loginButton);
        auth.add(new JLabel("Token:")); auth.add(tokenValue);
        add(auth, BorderLayout.SOUTH);

        JPanel center = new JPanel(new GridLayout(2, 1));

        JPanel telem = new JPanel(new GridLayout(4, 2, 8, 8));
        telem.setBorder(BorderFactory.createTitledBorder("Telemetría"));
        telem.add(new JLabel("Estación:")); telem.add(stationValue);
        telem.add(new JLabel("Velocidad:")); telem.add(speedValue);
        telem.add(new JLabel("Batería:")); telem.add(batteryValue);
        telem.add(new JLabel("Dirección:")); telem.add(directionValue);
        center.add(telem);

        JPanel cmds = new JPanel(new FlowLayout(FlowLayout.LEFT));
        cmds.setBorder(BorderFactory.createTitledBorder("Comandos"));
        cmds.add(speedUpBtn); cmds.add(slowDownBtn); cmds.add(stopNowBtn); cmds.add(startNowBtn);
        center.add(cmds);

        add(center, BorderLayout.CENTER);

        connectButton.addActionListener(e -> connect());
        disconnectButton.addActionListener(e -> disconnect());
        loginButton.addActionListener(e -> login());
        speedUpBtn.addActionListener(e -> sendCommand("SPEED UP"));
        slowDownBtn.addActionListener(e -> sendCommand("SLOW DOWN"));
        stopNowBtn.addActionListener(e -> sendCommand("STOPNOW"));
        startNowBtn.addActionListener(e -> sendCommand("STARTNOW"));

        pack();
        setLocationRelativeTo(null);
    }

    private void connect() {
        if (running) return;
        try {
            String host = hostField.getText().trim();
            int port = Integer.parseInt(portField.getText().trim());
            socket = new Socket(host, port);
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, ex.getMessage(), "Error de conexión", JOptionPane.ERROR_MESSAGE);
            return;
        }
        running = true;
        recvThread = new Thread(this::recvLoop, "admin-recv");
        recvThread.setDaemon(true);
        recvThread.start();
    }

    private void disconnect() {
        running = false;
        try { if (socket != null) socket.close(); } catch (Exception ignored) {}
        socket = null;
    }

    private void login() {
        if (socket == null) {
            JOptionPane.showMessageDialog(this, "Conéctate primero", "Conexión", JOptionPane.WARNING_MESSAGE);
            return;
        }
        String user = userField.getText().trim();
        String pass = new String(passField.getPassword());
        String payload = "USER=" + user + ";PASS=" + pass;
        String msg = "TYPE: LOGIN\nTOKEN: NULL\nPAYLOAD_LENGTH: " + payload.length() + "\nPAYLOAD:\n" + payload;
        try {
            OutputStream out = socket.getOutputStream();
            out.write(msg.getBytes());
            out.flush();
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, ex.getMessage(), "Error", JOptionPane.ERROR_MESSAGE);
        }
    }

    private void sendCommand(String command) {
        String token = tokenValue.getText().trim();
        if (token.equals("-") || token.isEmpty() || token.equals("NULL")) {
            JOptionPane.showMessageDialog(this, "Inicia sesión para obtener TOKEN", "Auth", JOptionPane.WARNING_MESSAGE);
            return;
        }
        String msg = "TYPE: COMMAND\nTOKEN: " + token + "\nPAYLOAD_LENGTH: " + command.length() + "\nPAYLOAD:\n" + command;
        try {
            OutputStream out = socket.getOutputStream();
            out.write(msg.getBytes());
            out.flush();
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, ex.getMessage(), "Error", JOptionPane.ERROR_MESSAGE);
        }
    }

    private void recvLoop() {
        byte[] buf = new byte[4096];
        int n;
        StringBuilder acc = new StringBuilder();
        try (InputStream in = socket.getInputStream()) {
            while (running && (n = in.read(buf)) != -1) {
                acc.append(new String(buf, 0, n));
                int idx = acc.indexOf("PAYLOAD:\n");
                if (idx >= 0) {
                    String header = acc.substring(0, idx);
                    // Capturar TOKEN de respuesta de LOGIN
                    for (String line : header.split("\n")) {
                        if (line.startsWith("TOKEN:")) {
                            String tok = line.split(":", 2)[1].trim();
                            if (!tok.isEmpty() && !tok.equals("NULL") && tokenValue.getText().equals("-")) {
                                SwingUtilities.invokeLater(() -> tokenValue.setText(tok));
                            }
                        }
                    }
                    String rest = acc.substring(idx + 9);
                    processPayload(rest);
                    acc.setLength(0);
                }
            }
        } catch (Exception ignored) {
        }
        SwingUtilities.invokeLater(() -> JOptionPane.showMessageDialog(this, "Desconectado del servidor", "Conexión", JOptionPane.INFORMATION_MESSAGE));
        disconnect();
    }

    private void processPayload(String payload) {
        String trimmed = payload.trim();
        if (trimmed.equals("OK")) return;
        if (trimmed.startsWith("ERROR")) {
            SwingUtilities.invokeLater(() -> JOptionPane.showMessageDialog(this, trimmed, "Servidor", JOptionPane.WARNING_MESSAGE));
            return;
        }
        String station = extract(payload, "STATION");
        String speed = extract(payload, "SPEED");
        String battery = extract(payload, "BATTERY");
        String direction = extract(payload, "DIRECTION");

        SwingUtilities.invokeLater(() -> {
            if (!station.isEmpty()) stationValue.setText(station);
            if (!speed.isEmpty()) speedValue.setText(speed);
            if (!battery.isEmpty()) batteryValue.setText(battery);
            if (!direction.isEmpty()) directionValue.setText(direction);
        });
    }

    private static String extract(String text, String key) {
        String prefix = key + "=";
        for (String line : text.split("\n")) {
            if (line.startsWith(prefix)) return line.substring(prefix.length()).trim();
        }
        return "";
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> new Admin().setVisible(true));
    }
}


