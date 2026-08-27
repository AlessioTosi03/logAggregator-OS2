#include "common.h"

/* Global server state and open file descriptors */
static volatile sig_atomic_t g_running = 1; /* Flag: 1 while running, 0 when user presses Ctrl+C */
static int g_server_fd = -1;                /* Server network socket listening for connections */
static int g_log_fd = -1;                   /* File descriptor for writing logs */
static char g_log_file[256] = LOG_FILE;     /* Active log filename */
static off_t g_max_size = MAX_LOG_SIZE;     /* File size limit before rotating */
static int g_interval = ALARM_INTERVAL;     /* Timer interval for rotation check */
static int g_port = PORT;                   /* Port number to listen on */

/* Helper: formats a log message with timestamp and writes it under exclusive file lock */
static void write_log_entry(const char *id, const char *data) {
    if (g_log_fd < 0) return;
    char ts[64], line[512];
    get_timestamp_safe(ts, sizeof(ts));
    format_log_safe(line, sizeof(line), ts, id, data);

    /* Lock file -> write line -> unlock file */
    if (lock_log(g_log_fd) == 0) {
        safe_write(g_log_fd, line, strlen(line));
        unlock_log(g_log_fd);
    }
}

/* --- ASYNC-SIGNAL-SAFE SIGNAL HANDLERS --- */

/* SIGALRM: Periodically checks log file size and rotates it when it gets too big */
static void handle_sigalrm(int sig) {
    (void)sig;
    if (g_log_fd >= 0) {
        struct stat st;
        /* Check current log size against maximum limit */
        if (fstat(g_log_fd, &st) == 0 && st.st_size >= g_max_size) {
            if (lock_log(g_log_fd) == 0) {
                char ts[64], archive[512];
                get_timestamp_safe(ts, sizeof(ts));
                /* Replace spaces/colons with underscores for valid backup filename */
                for (size_t i = 0; ts[i]; i++) {
                    if (ts[i] == ' ' || ts[i] == ':') ts[i] = '_';
                }

                /* Rename current log to backup (e.g., aggregated.log.2026-08-27_21_24_00.bak) */
                snprintf(archive, sizeof(archive), "%s.%s.bak", g_log_file, ts);
                if (rename(g_log_file, archive) == 0) {
                    /* Create a fresh new log file and redirect g_log_fd to it */
                    int new_fd = open(g_log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (new_fd >= 0) {
                        dup2(new_fd, g_log_fd); /* Atomically replace old fd */
                        safe_close(new_fd);
                    }
                }
                unlock_log(g_log_fd);
            }
        }
    }
    alarm(g_interval); /* Rearm the alarm timer for next check */
}

/* SIGPIPE: Handles sudden client socket disconnection during write */
static void handle_sigpipe(int sig) {
    (void)sig;
    write_log_entry("SIGPIPE_EVENT", "DISCONNECT");
}

/* SIGINT: Signal handler for Ctrl+C to initiate graceful shutdown */
static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0; /* Tell server loop to stop accepting */
    if (g_server_fd >= 0) {
        int fd = g_server_fd;
        g_server_fd = -1;
        safe_close(fd); /* Closing listener unblocks safe_accept() immediately */
    }
}

/* Registers signal handlers for alarms, broken pipes, and termination */
static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    /* Periodic log rotation timer */
    sa.sa_handler = handle_sigalrm;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    /* Broken pipe event handler */
    sa.sa_handler = handle_sigpipe;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &sa, NULL);

    /* Ctrl+C termination (no SA_RESTART so accept() unblocks) */
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

/* Worker thread: handles incoming logs from a single connected producer */
static void *worker_thread(void *arg) {
    int cfd = *(int *)arg;
    free(arg); /* Free heap memory allocated for socket descriptor */

    char sender_id[128] = "UNKNOWN";
    char buf[BUFFER_SIZE], line[BUFFER_SIZE];
    size_t line_len = 0;

    /* Read incoming messages until client disconnects or server stops */
    while (g_running) {
        ssize_t n = safe_read(cfd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            /* If connection closed or broken, record disconnect event in log */
            if (n == 0 || errno == EPIPE || errno == ECONNRESET) {
                write_log_entry(sender_id, "DISCONNECT");
            }
            break;
        }

        /* Parse stream byte-by-byte into complete newline-delimited lines */
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    char id[128], val[128];
                    /* Extract "SENDER_ID DATA" from the line */
                    if (sscanf(line, "%127s %127s", id, val) == 2) {
                        strncpy(sender_id, id, sizeof(sender_id) - 1);
                        write_log_entry(id, val);
                    } else if (sscanf(line, "%127s", id) == 1) {
                        strncpy(sender_id, id, sizeof(sender_id) - 1);
                        write_log_entry(id, "N/A");
                    }
                    line_len = 0; /* Reset line buffer for next message */
                }
            } else if (line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            }
        }
    }

    safe_close(cfd); /* Close client socket */
    return NULL;
}

int main(int argc, char *argv[]) {
    int opt;

    /* Parse command-line flags: -p <port>, -l <logfile>, -m <max_size>, -t <interval> */
    while ((opt = getopt(argc, argv, "p:l:m:t:")) != -1) {
        switch (opt) {
            case 'p': g_port = atoi(optarg); break;
            case 'l': strncpy(g_log_file, optarg, sizeof(g_log_file) - 1); break;
            case 'm': g_max_size = atol(optarg); break;
            case 't': g_interval = atoi(optarg); break;
        }
    }

    /* Open the log file in append mode (create if it does not exist) */
    g_log_fd = open(g_log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_log_fd < 0) { perror("open log"); exit(EXIT_FAILURE); }

    /* Set up all signal handlers (SIGALRM, SIGPIPE, SIGINT) */
    setup_signals();

    /* Create TCP server socket */
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /* Allow restarting server immediately without "port already in use" errors */
    int reuse = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Configure server IP address and port */
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(g_port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    /* Bind socket to port and start listening for connections */
    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(g_server_fd, 128) < 0) {
        perror("bind/listen"); exit(EXIT_FAILURE);
    }

    /* Start the periodic alarm timer for log rotation */
    alarm(g_interval);
    printf("[COORDINATOR] Listening on port %d...\n", g_port);

    /* Main server loop: accept incoming client connections */
    while (g_running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = safe_accept(g_server_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd < 0) {
            if (!g_running || errno == EBADF) break; /* Server is stopping */
            if (errno == EINTR) continue;            /* Signal occurred, retry */
            perror("accept");
            continue;
        }

        /* Allocate memory to safely pass socket descriptor to worker thread */
        int *pfd = malloc(sizeof(int));
        if (!pfd) { safe_close(client_fd); continue; }
        *pfd = client_fd;

        /* Spawn a dedicated thread for each connected producer */
        pthread_t tid;
        if (pthread_create(&tid, NULL, worker_thread, pfd) != 0) {
            safe_close(client_fd);
            free(pfd);
        } else {
            pthread_detach(tid); /* Let thread clean up automatically when done */
        }
    }

    printf("[COORDINATOR] Stopping server...\n");
    alarm(0); /* Disable periodic alarm timer */

    /* Cleanup server resources upon exit */
    if (g_server_fd >= 0) safe_close(g_server_fd);
    if (g_log_fd >= 0) {
        lock_log(g_log_fd);
        safe_close(g_log_fd);
    }
    printf("[COORDINATOR] Shutdown completed.\n");
    return 0;
}
