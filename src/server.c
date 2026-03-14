#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <termios.h>
#include <sys/stat.h>

#define SOCKET_PATH "/tmp/cis_test.sock"

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

double now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

void compute_statistics(const char *filename) {
    FILE *log = fopen(filename, "r");
    if (!log) return;

    char line[256];
    fgets(line, sizeof(line), log);

    double latencies[1000];
    int count = 0;
    double sum = 0;

    while (fgets(line, sizeof(line), log) && count < 1000) {
        double t1, t2, lat;
        char ch;
        int bytes;
        
        if (sscanf(line, "%lf,%c,%lf,%lf", &t1, &ch, &t2, &lat) == 4 ||
            sscanf(line, "%lf,%d,%lf,%lf", &t1, &bytes, &t2, &lat) == 4) {
            latencies[count] = lat;
            sum += lat;
            count++;
        }
    }
    fclose(log);

    if (count == 0) return;

    double mean = sum / count;

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (latencies[i] > latencies[j]) {
                double tmp = latencies[i];
                latencies[i] = latencies[j];
                latencies[j] = tmp;
            }
        }
    }

    double median = latencies[count / 2];
    double p99 = latencies[(int)(count * 0.99)];
    double min = latencies[0];
    double max = latencies[count - 1];

    log = fopen(filename, "a");
    fprintf(log, "\n");
    fprintf(log, "Summary Statistics:\n");
    fprintf(log, "- Total measurements: %d\n", count);
    fprintf(log, "- Mean latency: %.2f ms\n", mean);
    fprintf(log, "- Median (p50): %.2f ms\n", median);
    fprintf(log, "- p99: %.2f ms\n", p99);
    fprintf(log, "- Min: %.2f ms\n", min);
    fprintf(log, "- Max: %.2f ms\n", max);
    fclose(log);

    printf("\r\n[%s] Count: %d, Mean: %.2f ms, Median: %.2f ms, p99: %.2f ms\r\n",
           filename, count, mean, median, p99);
}

int main() {
    int master;
    pid_t pid = forkpty(&master, NULL, NULL, NULL);

    if (pid < 0) {
        perror("forkpty");
        exit(1);
    }

    if (pid == 0) {
        execl("/bin/bash", "bash", NULL);
        exit(1);
    }

    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(SOCKET_PATH);
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, 1);

    printf("[Server] Waiting for client on %s\r\n", SOCKET_PATH);

    int client = accept(server_sock, NULL, NULL);
    if (client < 0) {
        perror("accept");
        exit(1);
    }

    printf("[Server] Client connected!\r\n");
    printf("[Server] Type commands. Press Ctrl+Q to exit.\r\n\r\n");

    enable_raw_mode();

    mkdir("appendix", 0755);
    FILE *ctrl_log = fopen("appendix/latency.txt", "w");
    FILE *bcast_log = fopen("appendix/broadcast_latency.txt", "w");

    fprintf(ctrl_log, "keystroke_time,char,pty_echo_time,latency_ms\n");
    fprintf(bcast_log, "pty_read_time,bytes,socket_write_time,latency_ms\n");
    fflush(ctrl_log);
    fflush(bcast_log);

    double last_key_time = 0;
    char last_char = 0;
    int ctrl_count = 0;
    int bcast_count = 0;
    char buf[4096];

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(master, &readfds);

        int max_fd = (master > STDIN_FILENO) ? master : STDIN_FILENO;

        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;

            for (int i = 0; i < n; i++) {
                if (buf[i] == 17 || buf[i] == 4) {
                    printf("\r\n[Server] Exiting...\r\n");
                    goto cleanup;
                }
            }

            last_key_time = now_ms();
            last_char = buf[0];
            write(master, buf, n);
        }

        if (FD_ISSET(master, &readfds)) {
            double t1 = now_ms();
            int n = read(master, buf, sizeof(buf));
            if (n <= 0) break;

            write(client, buf, n);
            double t2 = now_ms();
            write(STDOUT_FILENO, buf, n);

            if (last_key_time > 0 && ctrl_count < 200) {
                double ctrl_latency = t1 - last_key_time;
                fprintf(ctrl_log, "%.3f,%c,%.3f,%.3f\n",
                        last_key_time, last_char, t1, ctrl_latency);
                fflush(ctrl_log);
                last_key_time = 0;
                ctrl_count++;
            }

            if (bcast_count < 200) {
                double bcast_latency = t2 - t1;
                fprintf(bcast_log, "%.3f,%d,%.3f,%.3f\n",
                        t1, n, t2, bcast_latency);
                fflush(bcast_log);
                bcast_count++;
            }
        }
    }

cleanup:
    fclose(ctrl_log);
    fclose(bcast_log);
    close(client);
    close(server_sock);
    unlink(SOCKET_PATH);

    printf("\r\n[Server] Computing statistics...\r\n");
    compute_statistics("appendix/latency.txt");
    compute_statistics("appendix/broadcast_latency.txt");
    printf("\r\n[Server] Logs written to appendix/\r\n");

    return 0;
}