import javax.swing.*;
import java.awt.*;
import java.io.InputStream;
import java.net.Socket;

public class Observer extends JFrame {
    private final JTextField hostField = new JTextField("98.84.165.222", 12);
    private final JTextField portField = new JTextField("8080", 6);
    private final JButton connectButton = new JButton("Conectar");
    private final JButton disconnectButton = new JButton("Desconectar");

    private final JLabel stationValue = new JLabel("-");
    private final JLabel speedValue = new JLabel("-");
    private final JLabel batteryValue = new JLabel("-");
    private final JLabel directionValue = new JLabel("-");

    private volatile boolean running = false;
    private Socket socket;
    private Thread recvThread;

    public Observer() {
        super("Metro Observer - Java");
        setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);
        setLayout(new BorderLayout());

        JPanel conn = new JPanel(new FlowLayout(FlowLayout.LEFT));
        conn.setBorder(BorderFactory.createTitledBorder("Conexión"));
        conn.add(new JLabel("Host:"));
        conn.add(hostField);
        conn.add(new JLabel("Puerto:"));
        conn.add(portField);
        conn.add(connectButton);
        conn.add(disconnectButton);
        add(conn, BorderLayout.NORTH);

        JPanel telem = new JPanel(new GridLayout(4, 2, 8, 8));
        telem.setBorder(BorderFactory.createTitledBorder("Telemetría"));
        telem.add(new JLabel("Estación:")); telem.add(stationValue);
        telem.add(new JLabel("Velocidad:")); telem.add(speedValue);
        telem.add(new JLabel("Batería:")); telem.add(batteryValue);
        telem.add(new JLabel("Dirección:")); telem.add(directionValue);
        add(telem, BorderLayout.CENTER);

        connectButton.addActionListener(e -> connect());
        disconnectButton.addActionListener(e -> disconnect());

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
        recvThread = new Thread(this::recvLoop, "observer-recv");
        recvThread.setDaemon(true);
        recvThread.start();
    }

    private void disconnect() {
        running = false;
        try { if (socket != null) socket.close(); } catch (Exception ignored) {}
        socket = null;
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
        SwingUtilities.invokeLater(() -> new Observer().setVisible(true));
    }
}


