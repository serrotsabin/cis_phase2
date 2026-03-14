#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_CLIENTS 10
#define MAX_USERNAME 32
#define SOCKET_PATH "/tmp/cis.sock"

// Message types
#define MSG_REQUEST_CONTROL  1
#define MSG_RELEASE_CONTROL  2
#define MSG_GRANT_CONTROL    3
#define MSG_REVOKE_CONTROL   4
#define MSG_PTY_OUTPUT       5
#define MSG_USER_JOINED      6
#define MSG_USER_LEFT        7
#define MSG_LIST_USERS       8

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