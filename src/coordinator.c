#include "common.h"

static volatile sig_atomic_t g_running = 1;
static int g_server_fd = -1;
static int g_log_fd = -1;

/* Helper function to format and write entry under lock */
static void write_log_entry(const char *id, const char *data) {
    if (g_log_fd < 0) return;
    char ts[64], line[512];
    get_timestamp_safe(ts, sizeof(ts));
    format_log_safe(line, sizeof(line), ts, id, data);

    if (lock_log(g_log_fd) == 0) {
        safe_write(g_log_fd, line, strlen(line));
        unlock_log(g_log_fd);
    }
}

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
    if (g_server_fd >= 0) {
        int fd = g_server_fd;
        g_server_fd = -1;
        safe_close(fd);
    }
}

/* Worker thread handling a producer connection */
static void *worker_thread(void *arg) {
    int cfd = *(int *)arg;
    free(arg);

    char sender_id[128] = "UNKNOWN";
    char buf[BUFFER_SIZE], line[BUFFER_SIZE];
    size_t line_len = 0;

    while (g_running) {
        ssize_t n = safe_read(cfd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            if (n == 0 || errno == EPIPE || errno == ECONNRESET) {
                write_log_entry(sender_id, "DISCONNECT");
            }
            break;
        }

        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    char id[128], val[128];
                    if (sscanf(line, "%127s %127s", id, val) == 2) {
                        strncpy(sender_id, id, sizeof(sender_id) - 1);
                        write_log_entry(id, val);
                    } else if (sscanf(line, "%127s", id) == 1) {
                        strncpy(sender_id, id, sizeof(sender_id) - 1);
                        write_log_entry(id, "N/A");
                    }
                    line_len = 0;
                }
            } else if (line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            }
        }
    }

    safe_close(cfd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    char log_file[128] = LOG_FILE;
    int opt;

    while ((opt = getopt(argc, argv, "p:l:")) != -1) {
        switch (opt) {
            case 'p': port = atoi(optarg); break;
            case 'l': strncpy(log_file, optarg, sizeof(log_file) - 1); break;
        }
    }

    g_log_fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_log_fd < 0) { perror("open log"); exit(EXIT_FAILURE); }

    struct sigaction sa = { .sa_handler = handle_sigint };
    sigaction(SIGINT, &sa, NULL);

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int reuse = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(g_server_fd, 128) < 0) {
        perror("bind/listen"); exit(EXIT_FAILURE);
    }

    printf("[COORDINATOR] Listening on port %d...\n", port);

    while (g_running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = safe_accept(g_server_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd < 0) {
            if (!g_running || errno == EBADF) break;
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        int *pfd = malloc(sizeof(int));
        if (!pfd) { safe_close(client_fd); continue; }
        *pfd = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, worker_thread, pfd) != 0) {
            safe_close(client_fd);
            free(pfd);
        } else {
            pthread_detach(tid);
        }
    }

    if (g_server_fd >= 0) safe_close(g_server_fd);
    if (g_log_fd >= 0) safe_close(g_log_fd);
    printf("[COORDINATOR] Shutdown completed.\n");
    return 0;
}
