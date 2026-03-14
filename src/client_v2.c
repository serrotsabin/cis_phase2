#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <termios.h>
#include <poll.h>

#define SOCKET_PATH "/tmp/cis.sock"

struct termios orig_termios;

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

int main() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        printf("Is the server running?\n");
        exit(1);
    }
    
    printf("[Client] Connected to CIS session\n");
    
    // Enable raw mode so keystrokes are sent immediately
    enable_raw_mode();
    
    struct pollfd fds[2];
    char buf[4096];
    
    while (1) {
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
            if (n > 0) {
                // Check for Ctrl+Q (quit)
                if (n == 1 && buf[0] == 17) {  // Ctrl+Q
                    printf("\r\n[Client] Quitting...\r\n");
                    break;
                }
                write(sock, buf, n);
            }
        }
        
        // Server sent output → display
        if (fds[1].revents & POLLIN) {
            int n = read(sock, buf, sizeof(buf));
            if (n <= 0) {
                printf("\r\n[Client] Session ended\r\n");
                break;
            }
            write(STDOUT_FILENO, buf, n);
        }
    }
    
    close(sock);
    return 0;
}