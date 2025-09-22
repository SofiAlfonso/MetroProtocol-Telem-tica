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
    for (int i = 0; i < session_count; i++) {
        if (strcmp(sessions[i].token, token) == 0) {
            return 1;
        }
    }
    return 0;
}

int parse_message(const char *buf, size_t n, message_t *msg) {
    // Copiar en buffer temporal
    char *tmp = strndup(buf, n);
    if (!tmp) return -1;

    char *line = strtok(tmp, "\n");
    int stage = 0;
    while (line) {
        if (strncmp(line, "TYPE:", 5) == 0) {
            strncpy(msg->type, line+6, sizeof(msg->type));
        } else if (strncmp(line, "TOKEN:", 6) == 0) {
            strncpy(msg->token, line+7, sizeof(msg->token));
        } else if (strncmp(line, "PAYLOAD_LENGTH:", 15) == 0) {
            msg->payload_len = atoi(line+16);
        } else if (strncmp(line, "PAYLOAD:", 8) == 0) {
            // Payload comienza en la siguiente línea
            char *payload_start = strstr(buf, "PAYLOAD:\n");
            if (payload_start) {
                payload_start += 9;
                msg->payload = strndup(payload_start, msg->payload_len);
            }
            break;
        }
        line = strtok(NULL, "\n");
        stage++;
    }
    free(tmp);
    return 0;
}

/* Handler para el hilo del cliente*/
void *client_handler(void *arg) {
    client_info_t *ci = (client_info_t *)arg;
    int fd = ci->fd;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, ci->ip, sizeof(client_ip));
    int client_port = ci->port;
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

        /* Log del mensaje crudo */
        log_msg("Recibido de %s:%d ->\n%s", client_ip, client_port, buf);

        /* Parsear usando parse_message() */
        message_t msg = {0};
        if (parse_message(buf, n, &msg) < 0) {
            char *resp = "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 20\nPAYLOAD:\nFormato inválido\n";
            send(fd, resp, strlen(resp), 0);
            log_msg("Enviado ERROR a %s:%d (formato inválido)", client_ip, client_port);
            continue;
        }

        /* Procesar por TYPE */
        if (strcmp(msg.type, "LOGIN") == 0) {
            /* Ejemplo de payload: USER=admin;PASS=1234 */
            char user[32], pass[32];
            if (sscanf(msg.payload, "USER=%31[^;];PASS=%31s", user, pass) == 2) {
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
                    strcpy(sessions[session_count].token, token);
                    strcpy(sessions[session_count].user, user);
                    session_count++;

                    char resp[256];
                    snprintf(resp, sizeof(resp),
                        "TYPE: RESPONSE\nTOKEN: %s\nPAYLOAD_LENGTH: 2\nPAYLOAD:\nOK\n", token);
                    send(fd, resp, strlen(resp), 0);
                    log_msg("Login correcto para %s, token=%s", user, token);
                } else {
                    char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 19\nPAYLOAD:\nERROR Credenciales\n";
                    send(fd, resp, strlen(resp), 0);
                    log_msg("Login fallido para user=%s", user);
                }
            } else {
                char *resp = "TYPE: ERROR\nTOKEN: NULL\nPAYLOAD_LENGTH: 18\nPAYLOAD:\nFormato LOGIN inválido\n";
                send(fd, resp, strlen(resp), 0);
            }

        } else if (strcmp(msg.type, "TELEMETRY") == 0) {
            if (msg.payload) {
                log_msg("TELEMETRY de %s:%d -> %s", client_ip, client_port, msg.payload);
                char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 2\nPAYLOAD:\nOK\n";
                send(fd, resp, strlen(resp), 0);
            }

        } else if (strcmp(msg.type, "COMMAND") == 0) {
            if (validate_token(msg.token)) {
                log_msg("COMMAND válido: %s", msg.payload);
                char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 2\nPAYLOAD:\nOK\n";
                send(fd, resp, strlen(resp), 0);
            } else {
                char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 16\nPAYLOAD:\nERROR Token inválido\n";
                send(fd, resp, strlen(resp), 0);
            }

        } else if (strcmp(msg.type, "LOGOUT") == 0) {
            char *resp = "TYPE: RESPONSE\nTOKEN: NULL\nPAYLOAD_LENGTH: 2\nPAYLOAD:\nOK\n";
            send(fd, resp, strlen(resp), 0);
            log_msg("Cliente %s:%d cerró sesión", client_ip, client_port);
            break;

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

    if (n == 0) {
        log_msg("Cliente %s:%d desconectó (fd=%d)", client_ip, client_port, fd);
    } else if (n < 0) {
        log_msg("Error recv de %s:%d: %s", client_ip, client_port, strerror(errno));
    }

    // Eliminar al cliente de la lista global
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

void *telemetry_thread(void *arg) {
    metro_state_t metro = {
        .current_station = 1,      // empezar en estación 1
        .total_stations = 5,
        .direction = 1,            // 1 = adelante, -1 = reversa
        .speed = 0,                // iniciar parado en estación 1
        .battery = 100,
        .stop_counter = 2           // parar 20s en la estación inicial
    };

    while (running) {
        sleep(10); // ciclo de telemetría cada 10s

        // ===== Lógica del Metro =====
        if (metro.stop_counter > 0) {
            metro.speed = 0;          // metro parado
            metro.stop_counter--;     // disminuir contador de parada
        } else {
            metro.speed = 40;         // velocidad normal
            metro.current_station += metro.direction;

            // Verificar estaciones terminales para invertir dirección
            if (metro.current_station >= metro.total_stations) {
                metro.current_station = metro.total_stations;
                metro.direction = -1;
            } else if (metro.current_station <= 1) {
                metro.current_station = 1;
                metro.direction = 1;
            }

            // Cada vez que llegamos a una estación, parar 20s
            metro.stop_counter = 2;
        }

        // Simular ligera variación de batería
        metro.battery -= 1;
        if (metro.battery < 20) metro.battery = 90; // recarga simulada

        // ===== Preparar payload =====
        char payload[256];
        snprintf(payload, sizeof(payload),
                 "STATION=%d\nSPEED=%d\nBATTERY=%d\nDIRECTION=%s\n",
                 metro.current_station,
                 metro.speed,
                 metro.battery,
                 metro.direction == 1 ? "FORWARD" : "REVERSE");

        char message[512];
        snprintf(message, sizeof(message),
                 "TYPE: TELEMETRY\nTOKEN: NULL\nPAYLOAD_LENGTH: %zu\nPAYLOAD:\n%s",
                 strlen(payload), payload);

        // ===== Enviar a todos los clientes =====
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            send(clients[i], message, strlen(message), 0);
        }
        pthread_mutex_unlock(&clients_mutex);

        // ===== Logging completo usando log_msg =====
        log_msg("BROADCAST TELEMETRY -> STATION=%d SPEED=%d BATTERY=%d DIRECTION=%s",
                metro.current_station, metro.speed, metro.battery,
                metro.direction == 1 ? "FORWARD" : "REVERSE");
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