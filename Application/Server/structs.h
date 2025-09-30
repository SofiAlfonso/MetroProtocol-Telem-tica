//
// Created by Ana Sofia Alfonso Moncada on 21/09/25.
//

#ifndef METROPROTOCOL_STRUCTS_H
#define METROPROTOCOL_STRUCTS_H

#include <netinet/in.h>

/* Estructura de un usuario admin */
typedef struct {
    char user[32];
    char pass[32];
} user_t;

/* Estructura de sesiones activas */
typedef struct {
    char token[64];
    char user[32];
} session_t;

/* Estructura de mensajes (comunicaciones) */
typedef struct {
    char type[32];
    char token[64];
    int payload_len;
    char *payload;
} message_t;

/* Estructura para el hilo cliente */
typedef struct {
    int fd;
    char ip[INET_ADDRSTRLEN];
    int port;
} client_info_t;

/* Estructura para el estado del metro*/
typedef struct {
    int current_station;
    int total_stations;
    int direction; // 1 = adelante, -1 = reversa
    int speed;     // km/h
    int battery;   // %
    int stop_counter;// contador de ciclos para pausa en estación
    int command_active;
    int speed_override;
    int progress;
    int charging;
} metro_state_t;

#endif //METROPROTOCOL_STRUCTS_H
