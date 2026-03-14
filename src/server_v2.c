#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <util.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <sys/stat.h>
#include "protocol.h"

// Global state
client_t clients[MAX_CLIENTS];
int num_clients = 0;
int current_controller = -1;

struct termios orig_termios;
int master_fd;
pid_t shell_pid;

// Terminal control
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

// Client management
int add_client(int fd, const char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].fd = fd;
            strncpy(clients[i].username, username, MAX_USERNAME - 1);
            clients[i].is_controller = 0;
            clients[i].active = 1;
            num_clients++;
            
            printf("\r\n[Server] Client '%s' joined (slot %d)\r\n", username, i);
            
            // First client becomes controller
            if (current_controller == -1) {
                clients[i].is_controller = 1;
                current_controller = i;
                printf("[Server] '%s' is now controller\r\n", username);
                
                // Send notification to client
                char msg[128];
                snprintf(msg, sizeof(msg), "\r\n[CIS] You are the controller. Type commands.\r\n");
                write(fd, msg, strlen(msg));
            } else {
                // Send observer notification
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
    
    close(clients[client_idx].fd);
    clients[client_idx].active = 0;
    num_clients--;
    
    // If controller left, grant to next client
    if (clients[client_idx].is_controller) {
        clients[client_idx].is_controller = 0;
        current_controller = -1;
        
        // Find next active client
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                clients[i].is_controller = 1;
                current_controller = i;
                printf("[Server] '%s' is now controller\r\n", clients[i].username);
                
                char msg[128];
                snprintf(msg, sizeof(msg), "\r\n[CIS] You are now the controller.\r\n");
                write(clients[i].fd, msg, strlen(msg));
                break;
            }
        }
    }
}

void broadcast_to_all(const char *data, int len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            int written = write(clients[i].fd, data, len);
            if (written < 0) {
                printf("[Server] Failed to write to client %d\n", i);
            }
        }
    }
}

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

int main() {
    // Initialize
    memset(clients, 0, sizeof(clients));
    
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
    
    // Enable raw mode on server terminal (so we can see what's happening)
    // BUT server terminal won't control the shell - only clients will
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
    printf("[Server] Socket: %s\r\n", SOCKET_PATH);
    printf("[Server] Waiting for clients...\r\n");
    
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
        
        // Poll with timeout for debugging
        int ret = poll(fds, nfds, 1000);  // 1 second timeout
        if (ret < 0) {
            perror("poll");
            break;
        }
        
        if (ret == 0) {
            // Timeout - just continue
            continue;
        }
        
        // Check PTY output (shell → all clients)
        if (fds[0].revents & POLLIN) {
            int n = read(master_fd, buf, sizeof(buf));
            if (n > 0) {
                // Broadcast to all clients AND display on server terminal
                write(STDOUT_FILENO, buf, n);  // Show on server
                broadcast_to_all(buf, n);      // Send to all clients
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
        
        // Check client input (clients → PTY if controller)
        for (int i = 2; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                int client_idx = client_poll_map[i - 2];
                int n = read(clients[client_idx].fd, buf, sizeof(buf));
                
                if (n > 0) {
                    // Only controller input goes to PTY
                    if (clients[client_idx].is_controller) {
                        printf("[Server] Controller input: %d bytes\r\n", n);
                        write(master_fd, buf, n);  // Send to shell
                    } else {
                        printf("[Server] Observer input ignored: %d bytes\r\n", n);
                    }
                } else {
                    // Client disconnected
                    remove_client(client_idx);
                }
            }
            
            if (fds[i].revents & (POLLHUP | POLLERR)) {
                int client_idx = client_poll_map[i - 2];
                remove_client(client_idx);
            }
        }
    }
    
    return 0;
}