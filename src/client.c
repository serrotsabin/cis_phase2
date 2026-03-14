#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#define SOCKET_PATH "/tmp/cis_test.sock"

int main() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        exit(1);
    }
    
    printf("[Observer] Connected! Watching session...\n");
    
    char buf[4096];
    while (1) {
        int n = read(sock, buf, sizeof(buf));
        if (n <= 0) {
            printf("\n[Observer] Session ended\n");
            break;
        }
        
        write(STDOUT_FILENO, buf, n);
    }
    
    close(sock);
    return 0;
}