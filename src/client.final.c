#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <termios.h>
#include <poll.h>

#define SOCKET_PATH "/tmp/cis.sock"

// Terminal state for client

struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\r\n");  // Clean newline before exit
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

// Client management functions

int main() {
    // Create socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }
    
    // Connect to server
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        printf("Is the server running?\n");
        exit(1);
    }
    
    printf("[Client] Connected to CIS session\r\n");
    printf("[Client] Controls:\r\n");
    printf("  Ctrl+T : Request control\r\n");
    printf("  Ctrl+R : Release control\r\n");
    printf("  Ctrl+L : List users\r\n");
    printf("  Ctrl+X : Quit\r\n");
    printf("\r\n");
    
    // Enable raw mode
    enable_raw_mode();
    
    // Main event loop
    struct pollfd fds[2];
    char buf[4096];
    
    while (1) {
        // Monitor stdin and socket
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        
        fds[1].fd = sock;
        fds[1].events = POLLIN;
        
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }
        
        // User typed something → send to server
        if (fds[0].revents & POLLIN) {
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            
            if (n == 0) {
                // EOF (Ctrl+D) - quit
                printf("\r\n[Client] EOF received, disconnecting...\r\n");
                close(sock);
                exit(0);
            }
            
            if (n > 0) {
                // Check for Ctrl+X (quit)
                for (int i = 0; i < n; i++) {
                    if (buf[i] == 24) {  // Ctrl+X
                        printf("\r\n[Client] Quitting...\r\n");
                        write(sock, "\x18", 1);  // Notify server
                        close(sock);
                        exit(0);
                    }
                }
                
                // Send to server
                write(sock, buf, n);
            }
        }
        
        // Server sent output → display
        if (fds[1].revents & POLLIN) {
            int n = read(sock, buf, sizeof(buf));
            if (n <= 0) {
                printf("\r\n[Client] Server disconnected\r\n");
                break;
            }
            write(STDOUT_FILENO, buf, n);
        }
        
        // Check for disconnect
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            printf("\r\n[Client] Connection lost\r\n");
            break;
        }
    }
    
    close(sock);
    return 0;
}