#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_CLIENTS 10
#define MAX_USERNAME 32
#define SOCKET_PATH "/tmp/cis.sock"

#define CONTROL_TIMEOUT 30  

// Client state
typedef struct {
    int fd;                    // Socket file descriptor
    char username[MAX_USERNAME];
    int is_controller;         // 1 if controller, 0 if observer
    int active;                // 1 if connected, 0 if slot free
} client_t;

// Control request queue
typedef struct {
    int client_index;
} request_t;

// Simple message structure
typedef struct {
    uint8_t type;
    uint16_t length;
    char data[4096];
} message_t;

#endif