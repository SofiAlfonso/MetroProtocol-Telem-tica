//
// Created by Ana Sofia Alfonso Moncada on 21/09/25.
//

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "structs.h"
#include "utils.h"

#define BACKLOG 10
#define MAXLINE 1024
#define MAX_CLIENTS 100

/* Server initial configs*/
static int listen_fd = -1;
static volatile int running = 1;
int clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_users = 2;
session_t sessions[100];
int session_count = 0;
user_t valid_users[] = {{"admin", "1234"},};
metro_state_t metro;
pthread_mutex_t metro_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Manejo de Ctrl+C para cerrar el socket de escucha */
void handle_sigint(int sign) {
    (void)sign;
    running = 0;
    log_msg("SIGINT recibido, cerrando servidor..." );
    if (listen_fd != -1) close(listen_fd);
}

void generate_token(char *buf, size_t len) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < len-1; i++) {
        buf[i] = charset[rand() % (sizeof(charset)-1)];
    }
    buf[len-1] = '\0';
}

int validate_token(const char *token) {
    if (!token || token[0] == '\0') return 0;
    int ok = 0;

    pthread_mutex_lock(&sessions_mutex);
    for (int i = 0; i < session_count; i++) {
        if (strcmp(sessions[i].token, token) == 0) {
            ok = 1;
            break;
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
    return ok;
}

int parse_message(const char *buf, size_t n, message_t *msg) {
    if (!buf || n == 0 || !msg) return -1;

    /* tmp contiene exactamente los n bytes y está NUL-terminated */
    char *tmp = strndup(buf, n);
    if (!tmp) return -1;

    /* Inicializar estructura */
    memset(msg, 0, sizeof(*msg));
    msg->payload = NULL;
    msg->payload_len = 0;

    /* recorrer líneas en tmp */
    char *saveptr = NULL;
    char *line = strtok_r(tmp, "\n", &saveptr);
    while (line) {
        /* eliminar posible '\r' final de la línea */
        size_t llen = strlen(line);
        if (llen > 0 && line[llen-1] == '\r') line[llen-1] = '\0';

        if (strncmp(line, "TYPE:", 5) == 0) {
            size_t src_off = 5;
            while (line[src_off] == ' ') src_off++;
            strncpy(msg->type, line + src_off, sizeof(msg->type) - 1);
            msg->type[sizeof(msg->type) - 1] = '\0';

        } else if (strncmp(line, "TOKEN:", 6) == 0) {
            size_t src_off = 6;
            while (line[src_off] == ' ') src_off++;
            strncpy(msg->token, line + src_off, sizeof(msg->token) - 1);
            msg->token[sizeof(msg->token) - 1] = '\0';

        } else if (strncmp(line, "PAYLOAD_LENGTH:", 15) == 0) {
            msg->payload_len = atoi(line + 15);
            if (msg->payload_len < 0) msg->payload_len = 0;
        }else if (strncmp(line, "PAYLOAD:", 8) == 0) {
                size_t src_off = 8;
                while (line[src_off] == ' ') src_off++;
                char *maybe_inline = line + src_off;

                if (*maybe_inline != '\0') {
                    // Inline payload después de PAYLOAD:
                    msg->payload = strndup(maybe_inline, msg->payload_len);
                } else {
                    // Payload empieza en la(s) línea(s) siguiente(s)
                    // saveptr sigue en strtok_r apuntando justo después del actual \n
                    char *payload_ptr = saveptr;
                    // Si el primer char es \r o \n, sáltalo
                    if (payload_ptr && (*payload_ptr == '\r' || *payload_ptr == '\n')) payload_ptr++;
                    if (payload_ptr) {
                        msg->payload = strndup(payload_ptr, msg->payload_len);
                    } else {
                        msg->payload = strdup("");
                    }
                }
                // Limpieza final: quitar \r/\n finales
                if (msg->payload) {
                    size_t len = strlen(msg->payload);
                    while (len > 0 && (msg->payload[len-1] == '\n' || msg->payload[len-1] == '\r')) {
                        msg->payload[len-1] = '\0';
                        len--;
                    }
                }
                break;
            }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(tmp);
    return 0;
}

void handle_command(char *cmd) {
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == '\r' || cmd[len-1] == ' ')) {
        cmd[len-1] = '\0';
        len--;
    }

    if (strcmp(cmd, "SPEED UP") == 0) {
        if (!metro.command_active && metro.stop_counter == 0 && metro.speed < 5) {
            metro.speed++;
            log_msg("Comando: SPEED UP -> velocidad=%d", metro.speed);
        } else {
            log_msg("Comando: SPEED UP ignorado (STOPNOW activo, en estación, o velocidad máxima alcanzada)");
        }

    } else if (strcmp(cmd, "SLOW DOWN") == 0) {
        if (!metro.command_active && metro.stop_counter == 0 && metro.speed > 1) {
            metro.speed--;
            log_msg("Comando: SLOW DOWN -> velocidad=%d", metro.speed);
        } else {
            log_msg("Comando: SLOW DOWN ignorado (STOPNOW activo, en estación, o velocidad mínima alcanzada)");
        }

    } else if (strcmp(cmd, "STOPNOW") == 0) {
        metro.speed = 0;             // detener metro
        metro.command_active = 1;    // marcar STOPNOW activo
        metro.stop_counter = 0;      // cancelar espera de estación si la había
        log_msg("Comando: STOPNOW -> metro detenido inmediatamente");

    } else if (strcmp(cmd, "STARTNOW") == 0) {
        if (metro.command_active && metro.speed == 0) {
            // Retomar tras STOPNOW
            metro.command_active = 0;
            metro.speed = 1;
            log_msg("Comando: STARTNOW -> metro retomando desde STOPNOW");
        } else if (metro.stop_counter > 0) {
            // Retomar desde estación
            metro.stop_counter = 0;
            metro.speed = 1;
            log_msg("Comando: STARTNOW -> salida inmediata desde estación");
        } else {
            log_msg("Comando: STARTNOW ignorado (no estaba en STOPNOW ni en estación)");
        }

    } else {
        log_msg("Comando desconocido: %s", cmd);
    }
}


/* Handler para el hilo del cliente */
void *client_handler(void *arg) {
    client_info_t *ci = (client_info_t *)arg;
    const int fd = ci->fd;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, ci->ip, sizeof(client_ip));
    const int client_port = ci->port;
    free(ci);

    log_msg("Cliente conectado: %s:%d (fd=%d)", client_ip, client_port, fd);

    // Registrar el cliente en la lista global
    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
        clients[client_count++] = fd;
    }
    pthread_mutex_unlock(&clients_mutex);

    char buf[4096];
    ssize_t n;

    while ((n = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        log_msg("Recibido de %s:%d ->\n%s", client_ip, client_port, buf);

        message_t msg = {0};
        if (parse_message(buf, n, &msg) < 0) {
            const char *resp = "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 20\nPAYLOAD:\nFormato inválido\n";
            send(fd, resp, strlen(resp), 0);
            log_msg("Enviado ERROR a %s:%d (formato inválido)", client_ip, client_port);
            continue;
        }

        if (strcmp(msg.type, "LOGIN") == 0) {
            if (!msg.payload) {
                const char *resp = "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 19\nPAYLOAD:\nPayload vacío\n";
                send(fd, resp, strlen(resp), 0);
                log_msg("LOGIN: payload vacío desde %s:%d", client_ip, client_port);
            } else {
                // 🔹 Sanitizar payload quitando saltos de línea y espacios finales
                // --- Sanitizar payload quitando saltos de línea y espacios finales de forma robusta
                size_t plen = strlen(msg.payload);
                while (plen > 0 &&
                      (msg.payload[plen-1] == '\n' ||
                       msg.payload[plen-1] == '\r' ||
                       msg.payload[plen-1] == ' '))
                {
                    msg.payload[plen-1] = '\0';
                    plen--;
                }

                // DEBUG
                log_msg("DEBUG-LOGIN: payload recibido='%s'", msg.payload);

                char user[32] = {0}, pass[32] = {0};
                // Usar formato robusto
                if (sscanf(msg.payload, "USER=%31[^;];PASS=%31[^\n\r ]", user, pass) == 2) {
                    int ok = 0;
                    for (int i = 0; i < num_users; i++) {
                        if (strcmp(valid_users[i].user, user) == 0 &&
                            strcmp(valid_users[i].pass, pass) == 0) {
                            ok = 1;
                            break;
                            }
                    }
                    if (ok) {
                        char token[64];
                        generate_token(token, sizeof(token));
                        pthread_mutex_lock(&sessions_mutex);
                        if (session_count < (int)(sizeof(sessions)/sizeof(sessions[0]))) {
                            strncpy(sessions[session_count].token, token, sizeof(sessions[0].token)-1);
                            sessions[session_count].token[sizeof(sessions[0].token)-1] = '\0';
                            strncpy(sessions[session_count].user, user, sizeof(sessions[0].user)-1);
                            sessions[session_count].user[sizeof(sessions[0].user)-1] = '\0';
                            session_count++;
                            pthread_mutex_unlock(&sessions_mutex);
                            char resp[256];
                            snprintf(resp, sizeof(resp),
                                     "TYPE: RESPONSE\nTOKEN: %s\nPAYLOAD_LENGTH: 2\nPAYLOAD:\nOK\n", token);
                            send(fd, resp, strlen(resp), 0);
                            log_msg("Login correcto para %s, token=%s", user, token);
                        } else {
                            pthread_mutex_unlock(&sessions_mutex);
                            const char *resp = "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 24\nPAYLOAD:\nServidor sin slots\n";
                            send(fd, resp, strlen(resp), 0);
                            log_msg("Login rechazado (sin slots) para %s", user);
                        }
                    } else {
                        const char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 19\nPAYLOAD:\nERROR Credenciales\n";
                        send(fd, resp, strlen(resp), 0);
                        log_msg("Login fallido para user=%s", user);
                    }
                } else {
                    // LOG extra
                    log_msg("DEBUG-LOGIN: Formato recibido no parseado correctamente");
                    const char *resp = "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 18\nPAYLOAD:\nFormato LOGIN inválido\n";
                    send(fd, resp, strlen(resp), 0);
                    log_msg("LOGIN: formato inválido desde %s:%d", client_ip, client_port);
                }
            }
        } else if (strcmp(msg.type, "COMMAND") == 0) {
            if (validate_token(msg.token)) {
                log_msg("COMMAND recibido: %s", msg.payload);
                pthread_mutex_lock(&metro_mutex);
                handle_command(msg.payload);
                pthread_mutex_unlock(&metro_mutex);

                const char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 2\nPAYLOAD:\nOK\n";
                send(fd, resp, strlen(resp), 0);
            } else {
                const char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 16\nPAYLOAD:\nERROR Token inválido\n";
                send(fd, resp, strlen(resp), 0);
            }

        }else if (strcmp(msg.type, "LOGOUT") == 0) {
            if (msg.token[0] == '\0') {
                const char *resp = "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 19\nPAYLOAD:\nToken vacío\n";
                send(fd, resp, strlen(resp), 0);
                log_msg("LOGOUT: token vacío desde %s:%d", client_ip, client_port);
            } else {
                int removed = 0;
                pthread_mutex_lock(&sessions_mutex);
                for (int i = 0; i < session_count; i++) {
                    if (strcmp(sessions[i].token, msg.token) == 0) {
                        /* Mover el último elemento al lugar del eliminado */
                        sessions[i] = sessions[session_count - 1];
                        session_count--;
                        removed = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&sessions_mutex);

                if (removed) {
                    const char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 2\nPAYLOAD:\nOK\n";
                    send(fd, resp, strlen(resp), 0);
                    log_msg("LOGOUT correcto para token=%s", msg.token);
                } else {
                    const char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 21\nPAYLOAD:\nERROR Token inválido\n";
                    send(fd, resp, strlen(resp), 0);
                    log_msg("LOGOUT fallido para token=%s", msg.token);
                }
            }
        } else {
            char resp[256];
            snprintf(resp, sizeof(resp),
                     "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 24\nPAYLOAD:\nTipo desconocido: %s\n",
                     msg.type);
            send(fd, resp, strlen(resp), 0);
            log_msg("Tipo desconocido: %s", msg.type);
        }

        if (msg.payload) free(msg.payload);
    }

    log_msg("Cliente %s:%d desconectó (fd=%d)", client_ip, client_port, fd);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == fd) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    close(fd);
    return NULL;
}

void *metro_thread(void *arg) {
    // tiempo base por tramo (en segundos, velocidad = 1)
    int travel_time_base[6] = {0, 100, 100, 100, 100, 100};

    while (running) {
        pthread_mutex_lock(&metro_mutex);

        // ⚡ Revisar batería
        if (!metro.charging && metro.speed > 0) {
            metro.battery -= metro.speed;
            if (metro.battery < 0) metro.battery = 0;
        }

        // 🚨 Si batería agotada → entrar en recarga
        if (metro.battery == 0 && !metro.charging) {
            metro.speed = 0;
            metro.charging = 1;
            metro.stop_counter = 0;  // ignorar paradas normales
            metro.command_active = 1; // evitar que comandos reactiven
        }

        // 🔋 Si está recargando
        if (metro.charging) {
            metro.battery += 10;
            if (metro.battery >= 100) {
                metro.battery = 100;
                metro.charging = 0;
                metro.command_active = 0;
                metro.speed = 1;  // reanudar marcha
            }
            pthread_mutex_unlock(&metro_mutex);
            sleep(2); // cada tick = 2s → +10% batería
            continue;
        }

        if (metro.command_active && metro.speed == 0) {
            // STOPNOW → metro detenido
            pthread_mutex_unlock(&metro_mutex);
            sleep(2);
            continue;
        }

        if (metro.stop_counter > 0) {
            // 🚉 detenido en estación
            metro.stop_counter--;
            if (metro.stop_counter == 0) {
                // al salir, siempre reinicia en velocidad 1 (si no está en STOPNOW)
                if (!metro.command_active) {
                    metro.speed = 1;
                }
            }
        } else {
            // calcular tiempo de viaje ajustado por velocidad
            int base_time = travel_time_base[metro.current_station];
            double travel_time = (double)base_time / metro.speed;
            double step = 100.0 / travel_time;  // % de avance por segundo

            metro.progress += step;

            if (metro.progress >= 100.0) {
                // llegó a siguiente estación
                metro.current_station += metro.direction;
                metro.progress = 0.0;

                // extremos → cambiar dirección
                if (metro.current_station >= metro.total_stations) {
                    metro.current_station = metro.total_stations;
                    metro.direction = -1;
                } else if (metro.current_station <= 1) {
                    metro.current_station = 1;
                    metro.direction = 1;
                }

                // parada de 20s
                metro.stop_counter = 10;
                metro.speed = 0;  // velocidad 0 mientras está detenido
            }
        }

        pthread_mutex_unlock(&metro_mutex);
        sleep(2); // cada iteración = 1 segundo
    }

    return NULL;
}

void *telemetry_thread(void *arg) {
    while (running) {
        sleep(10); // ciclo de telemetría cada 10s

        pthread_mutex_lock(&metro_mutex);

        char payload[256];
        snprintf(payload, sizeof(payload),
                 "STATION=%d\nSPEED=%d\nBATTERY=%d\nDIRECTION=%s\n",
                 metro.current_station,
                 metro.speed,
                 metro.battery,
                 metro.direction == 1 ? "FORWARD" : "REVERSE");

        pthread_mutex_unlock(&metro_mutex);

        // Enviar a clientes
        char message[512];
        snprintf(message, sizeof(message),
                 "TYPE: TELEMETRY\nTOKEN: NULL\nPAYLOAD_LENGTH: %zu\nPAYLOAD:\n%s",
                 strlen(payload), payload);

        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            send(clients[i], message, strlen(message), 0);
        }
        pthread_mutex_unlock(&clients_mutex);

        log_msg("BROADCAST TELEMETRY -> %s", payload);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <port> <LogsFile>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int port = atoi(argv[1]);
    const char *logfname = argv[2];

    logfile = fopen(logfname, "a");
    if (!logfile) {
        perror("fopen logs");
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);

    /* 1) Crear socket (Berkeley API) */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* 2) setsockopt SO_REUSEADDR */
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    /* 3) bind() */
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); /* aceptar en todas las interfaces */
    servaddr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    /* 4) listen() */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    log_msg("Servidor escuchando en puerto %d. Logs -> %s", port, logfname);

    /* Inicialización de metro */
    metro.current_station = 1;
    metro.total_stations = 5;
    metro.direction = 1;
    metro.speed = 1;
    metro.battery = 100;
    metro.stop_counter = 2;
    metro.command_active = 0;
    metro.speed_override =  -1;
    metro.progress = 0;

    pthread_t metro_tid;
    if (pthread_create(&metro_tid, NULL, metro_thread, NULL) != 0) {
        perror("pthread_create metro_thread");
    }

    /* Crear hilo de telemetría */
    pthread_t telemetry_tid;
    if (pthread_create(&telemetry_tid, NULL, telemetry_thread, NULL) != 0) {
        perror("pthread_create telemetry_thread");
        // No salimos, el servidor puede seguir funcionando sin telemetría
    }

    /* 5) accept() loop */
    while (running) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int connfd = accept(listen_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (connfd < 0) {
            if (!running) break; /* acept loop interrumpido por SIGINT */
            perror("accept");
            continue;
        }

        client_info_t *ci = malloc(sizeof(client_info_t));
        if (!ci) {
            close(connfd);
            continue;
        }
        ci->fd = connfd;
        inet_ntop(AF_INET, &cliaddr.sin_addr, ci->ip, sizeof(ci->ip));
        ci->port = ntohs(cliaddr.sin_port);

        /* Crear hilo para atender al cliente (detached) */
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, ci) != 0) {
            perror("pthread_create");
            close(connfd);
            free(ci);
            continue;
        }
        pthread_detach(tid);
    }

    /* Cleanup */
    log_msg("Cerrando socket de escucha...");
    if (listen_fd != -1) close(listen_fd);
    if (logfile) fclose(logfile);
    log_msg("Servidor terminado.");
    return 0;
}