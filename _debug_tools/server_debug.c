/**
 * CIS Phase 2 - Collaborative Interactive Shell Server
 * 
 * Description:
 *   Server that hosts a bash shell in a PTY and allows multiple clients
 *   to connect, observe output, and take turns controlling the session
 *   via a FIFO floor control protocol.
 * 
 * Architecture:
 *   - Single-threaded event loop using poll()
 *   - PTY for hosting bash shell (created with forkpty())
 *   - UNIX domain socket for client connections
 *   - FIFO queue for control requests
 * 
 * Authors: Abin Timilsina, Sabin Ghimire, Nuraj Rimal
 * Date: February 2026
 * Course: Operating Systems - SIUE
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>      /* Linux — change to <util.h> on macOS */
#include <time.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <sys/stat.h>
#include "protocol.h"

/* ── DEBUG LOGGING ────────────────────────────────────────────────────────────
 * Writes timestamped events to stderr so you can capture them into trace files.
 * This function is NOT in server.final.c — only in this debug build.
 * ──────────────────────────────────────────────────────────────────────────── */
static void dbg_log(const char *fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm_info = localtime(&ts.tv_sec);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);
    fprintf(stderr, "[%s.%03ld] ", tbuf, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
}


// ========== GLOBAL STATE ==========

// Clients
client_t clients[MAX_CLIENTS];
int num_clients = 0;
int current_controller = -1;

// Control request queue
request_t request_queue[MAX_CLIENTS];
int queue_head = 0;
int queue_tail = 0;

// Terminal state
struct termios orig_termios;

// PTY
int master_fd;
pid_t shell_pid;

// ========== TERMINAL CONTROL ==========

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// ========== BROADCAST FUNCTIONS ==========

void broadcast_to_all(const char *data, int len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            int written = write(clients[i].fd, data, len);
            dbg_log("broadcast: client_idx=%d fd=%d bytes=%d", i, clients[i].fd, len);
            if (written < 0) {
                printf("[Server] Failed to write to client %d\r\n", i);
            }
        }
    }
}

// ========== CONTROL QUEUE MANAGEMENT ==========

void enqueue_request(int client_idx) {
    // Check if already in queue
    for (int i = queue_head; i < queue_tail; i++) {
        if (request_queue[i % MAX_CLIENTS].client_index == client_idx) {
            printf("[Server] Client %s already in queue\r\n", 
                   clients[client_idx].username);
            return;
        }
    }
    
    // Check if already controller
    if (clients[client_idx].is_controller) {
        printf("[Server] Client %s already has control\r\n", 
               clients[client_idx].username);
        return;
    }
    
    // Add to queue
    request_queue[queue_tail % MAX_CLIENTS].client_index = client_idx;
    queue_tail++;
    
    int position = queue_tail - queue_head;
    printf("[Server] Client %s queued for control (position: %d)\r\n", 
           clients[client_idx].username, position);
    dbg_log("enqueue: client_idx=%d (%s) pos=%d head=%d tail=%d", client_idx, clients[client_idx].username, position, queue_head, queue_tail);
    
    // Send position notification to requester
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "\r\n[CIS] Control request sent. Position in queue: %d\r\n", 
             position);
    write(clients[client_idx].fd, msg, strlen(msg));
}

int dequeue_request() {
    if (queue_head >= queue_tail) {
        return -1;  // Queue empty
    }
    
    int client_idx = request_queue[queue_head % MAX_CLIENTS].client_index;
    queue_head++;
    
    return client_idx;
}

void grant_control(int client_idx) {
    if (client_idx < 0 || client_idx >= MAX_CLIENTS) return;
    if (!clients[client_idx].active) return;
    
    // Revoke current controller
    if (current_controller >= 0 && current_controller < MAX_CLIENTS) {
        if (clients[current_controller].active) {
            clients[current_controller].is_controller = 0;
            
            char msg[256];
            snprintf(msg, sizeof(msg), 
                     "\r\n[CIS] Control granted to %s\r\n", 
                     clients[client_idx].username);
            write(clients[current_controller].fd, msg, strlen(msg));
        }
    }
    
    // Grant to new controller
    clients[client_idx].is_controller = 1;
    current_controller = client_idx;
    
    printf("[Server] Control granted to %s\r\n", clients[client_idx].username);
    dbg_log("grant_control: client_idx=%d (%s) current_controller=%d", client_idx, clients[client_idx].username, current_controller);
    
    // Notify new controller
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "\r\n[CIS] You now have control. Press Ctrl+R to release.\r\n");
    write(clients[client_idx].fd, msg, strlen(msg));
    
    // Notify all other clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && i != client_idx) {
            snprintf(msg, sizeof(msg), 
                     "\r\n[CIS] Control granted to %s\r\n", 
                     clients[client_idx].username);
            write(clients[i].fd, msg, strlen(msg));
        }
    }
}

void release_control(int client_idx) {
    if (client_idx < 0 || client_idx >= MAX_CLIENTS) return;
    if (!clients[client_idx].active) return;
    if (!clients[client_idx].is_controller) return;
    
    printf("[Server] %s released control\r\n", clients[client_idx].username);
    dbg_log("release_control: client_idx=%d (%s) queue_head=%d queue_tail=%d", client_idx, clients[client_idx].username, queue_head, queue_tail);
    
    clients[client_idx].is_controller = 0;
    current_controller = -1;
    
    // Notify the releaser
    char msg[256];
    snprintf(msg, sizeof(msg), "\r\n[CIS] Control released.\r\n");
    write(clients[client_idx].fd, msg, strlen(msg));
    
    // Grant to next in queue
    int next = dequeue_request();
    if (next >= 0 && clients[next].active) {
        grant_control(next);
    } else {
        // No one waiting - notify all
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                snprintf(msg, sizeof(msg), 
                         "\r\n[CIS] No controller. Press Ctrl+T to request control.\r\n");
                write(clients[i].fd, msg, strlen(msg));
            }
        }
    }
}

void request_control(int client_idx) {
    if (client_idx < 0 || client_idx >= MAX_CLIENTS) return;
    if (!clients[client_idx].active) return;
    
    // Already controller?
    if (clients[client_idx].is_controller) {
        char msg[256];
        snprintf(msg, sizeof(msg), "\r\n[CIS] You already have control.\r\n");
        write(clients[client_idx].fd, msg, strlen(msg));
        return;
    }
    
    // No current controller? Grant immediately
    if (current_controller < 0) {
        grant_control(client_idx);
    } else {
        // Queue the request
        enqueue_request(client_idx);
    }
}

// ========== CLIENT MANAGEMENT ==========

int add_client(int fd, const char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].fd = fd;
            strncpy(clients[i].username, username, MAX_USERNAME - 1);
            clients[i].is_controller = 0;
            clients[i].active = 1;
            num_clients++;
            
            printf("\r\n[Server] Client '%s' joined (slot %d)\r\n", username, i);
            dbg_log("accept: fd=%d username=%s slot=%d num_clients=%d", fd, username, i, num_clients+1);
            
            // First client becomes controller
            if (current_controller == -1) {
                clients[i].is_controller = 1;
                current_controller = i;
                printf("[Server] '%s' is now controller\r\n", username);
                dbg_log("first client auto-granted controller: %s", username);
                
                char msg[128];
                snprintf(msg, sizeof(msg), "\r\n[CIS] You are the controller. Type commands.\r\n");
                write(fd, msg, strlen(msg));
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "\r\n[CIS] Connected as observer. Controller: %s\r\n", 
                         clients[current_controller].username);
                write(fd, msg, strlen(msg));
            }
            
            return i;
        }
    }
    return -1;
}

void remove_client(int client_idx) {
    if (client_idx < 0 || client_idx >= MAX_CLIENTS) return;
    if (!clients[client_idx].active) return;
    
    printf("\r\n[Server] Client '%s' disconnected\r\n", clients[client_idx].username);
    dbg_log("remove_client: client_idx=%d (%s) fd=%d was_controller=%d", client_idx, clients[client_idx].username, clients[client_idx].fd, clients[client_idx].is_controller);
    
    close(clients[client_idx].fd);
    clients[client_idx].active = 0;
    num_clients--;
    
    // If controller left, auto-release and grant to next
    if (clients[client_idx].is_controller) {
        clients[client_idx].is_controller = 0;
        current_controller = -1;
        
        printf("[Server] Controller disconnected, checking queue...\r\n");
        dbg_log("controller gone, queue_size=%d", queue_tail - queue_head);
        
        // Grant to next in queue
        int next = dequeue_request();
        if (next >= 0 && clients[next].active) {
            grant_control(next);
        } else {
            // No queue, find any active client
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].active) {
                    grant_control(i);
                    break;
                }
            }
        }
    } else {
        // Remove from queue if they were waiting
        int new_queue_tail = queue_head;
        for (int i = queue_head; i < queue_tail; i++) {
            int idx = request_queue[i % MAX_CLIENTS].client_index;
            if (idx != client_idx && clients[idx].active) {
                request_queue[new_queue_tail % MAX_CLIENTS].client_index = idx;
                new_queue_tail++;
            }
        }
        queue_tail = new_queue_tail;
    }
}

// ========== CLEANUP ==========

void cleanup() {
    printf("\r\n[Server] Shutting down...\r\n");
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            close(clients[i].fd);
        }
    }
    
    if (shell_pid > 0) {
        kill(shell_pid, SIGTERM);
    }
    
    unlink(SOCKET_PATH);
}

// ========== MAIN ==========

int main() {
    // Initialize
    memset(clients, 0, sizeof(clients));
    memset(request_queue, 0, sizeof(request_queue));
    
    // Create PTY
    shell_pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (shell_pid < 0) {
        perror("forkpty");
        exit(1);
    }
    
    if (shell_pid == 0) {
        // Child: become bash
        execl("/bin/bash", "bash", NULL);
        exit(1);
    }
    
    // Enable raw mode on server terminal
    enable_raw_mode();
    
    // Create socket
    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        exit(1);
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    unlink(SOCKET_PATH);
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    
    listen(server_sock, 5);
    
    printf("[Server] CIS server started\r\n");
    dbg_log("server started: master_fd=%d shell_pid=%d", master_fd, shell_pid);
    printf("[Server] Socket: %s\r\n", SOCKET_PATH);
    printf("[Server] Waiting for clients...\r\n");
    dbg_log("event loop started on %s", SOCKET_PATH);
    
    atexit(cleanup);
    signal(SIGINT, exit);
    signal(SIGTERM, exit);
    
    // Main event loop
    struct pollfd fds[MAX_CLIENTS + 2];
    char buf[4096];
    
    while (1) {
        int nfds = 0;
        
        // PTY master (shell output)
        fds[nfds].fd = master_fd;
        fds[nfds].events = POLLIN;
        nfds++;
        
        // Listening socket (new connections)
        fds[nfds].fd = server_sock;
        fds[nfds].events = POLLIN;
        nfds++;
        
        // All client sockets
        int client_poll_map[MAX_CLIENTS];
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                client_poll_map[nfds - 2] = i;
                fds[nfds].fd = clients[i].fd;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }
        
        // Poll
        int ret = poll(fds, nfds, 1000);
        if (ret < 0) {
            perror("poll");
            break;
        }
        
        if (ret == 0) {
            continue;
        }
        
        // Check PTY output (shell → all clients)
        if (fds[0].revents & POLLIN) {
            int n = read(master_fd, buf, sizeof(buf));
            dbg_log("pty_read: master_fd=%d", master_fd);
            if (n > 0) {
                write(STDOUT_FILENO, buf, n);
                broadcast_to_all(buf, n);
            } else if (n == 0) {
                printf("\r\n[Server] Shell exited\r\n");
                break;
            }
        }
        
        // Check new connections
        if (fds[1].revents & POLLIN) {
            int client_fd = accept(server_sock, NULL, NULL);
            if (client_fd >= 0) {
                char username[MAX_USERNAME];
                snprintf(username, sizeof(username), "user%d", num_clients + 1);
                
                int idx = add_client(client_fd, username);
                if (idx < 0) {
                    printf("[Server] Max clients reached\r\n");
                    close(client_fd);
                }
            }
        }
        
        // Check client input
        for (int i = 2; i < nfds; i++) {
            int client_idx = client_poll_map[i - 2];
            int client_needs_removal = 0;
            
            if (fds[i].revents & POLLIN) {
                int n = read(clients[client_idx].fd, buf, sizeof(buf));
                
                if (n > 0) {
                    // Process each byte for escape sequences
                    int ctrl_handled = 0;
                    
                    for (int j = 0; j < n; j++) {
                        unsigned char ch = buf[j];
                        
                        // Ctrl+T (20) - Request control
                        if (ch == 20) {
                            printf("[Server] %s pressed Ctrl+T (request control)\r\n", 
                                   clients[client_idx].username);
                            request_control(client_idx);
                            ctrl_handled = 1;
                        }
                        // Ctrl+R (18) - Release control
                        else if (ch == 18) {
                            printf("[Server] %s pressed Ctrl+R (release control)\r\n", 
                                   clients[client_idx].username);
                            release_control(client_idx);
                            ctrl_handled = 1;
                        }
                        // Ctrl+L (12) - List users
                        else if (ch == 12) {
                            printf("[Server] %s pressed Ctrl+L (list users)\r\n", 
                                   clients[client_idx].username);
                            
                            char list_msg[1024];
                            int offset = snprintf(list_msg, sizeof(list_msg), 
                                                 "\r\n[CIS] Connected users:\r\n");
                            
                            for (int k = 0; k < MAX_CLIENTS; k++) {
                                if (clients[k].active) {
                                    offset += snprintf(list_msg + offset, 
                                                     sizeof(list_msg) - offset,
                                                     "  - %s%s\r\n", 
                                                     clients[k].username,
                                                     clients[k].is_controller ? " (controller)" : "");
                                }
                            }
                            
                            if (queue_tail > queue_head) {
                                offset += snprintf(list_msg + offset, 
                                                 sizeof(list_msg) - offset,
                                                 "\r\nControl queue: %d waiting\r\n", 
                                                 queue_tail - queue_head);
                            }
                            
                            write(clients[client_idx].fd, list_msg, strlen(list_msg));
                            ctrl_handled = 1;
                        }
                        // Ctrl+X (24) - Quit
                        else if (ch == 24) {
                            printf("[Server] %s pressed Ctrl+X (disconnecting)\r\n", 
                                   clients[client_idx].username);
                            client_needs_removal = 1;
                            ctrl_handled = 1;
                            break;  // Exit byte loop
                        }
                    }
                    
                    // If not a control command and user is controller, send to PTY
                    if (!ctrl_handled && clients[client_idx].is_controller) {
                        dbg_log("pty_write: controller=%s bytes=%d", clients[client_idx].username, n);
                        write(master_fd, buf, n);
                    } else if (!ctrl_handled && !clients[client_idx].is_controller) {
                        dbg_log("observer_drop: %s sent %d bytes - not forwarded to PTY", clients[client_idx].username, n);
                    }
                    
                    // Handle removal after processing
                    if (client_needs_removal) {
                        remove_client(client_idx);
                    }
                } else {
                    // Read returned 0 or -1
                    remove_client(client_idx);
                }
            }
            
            if (fds[i].revents & (POLLHUP | POLLERR)) {
                remove_client(client_idx);
            }
        }
    }
    
    return 0;
}