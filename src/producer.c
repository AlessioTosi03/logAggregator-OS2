#include "common.h"

int main(int argc, char *argv[]) {
    /* Default settings for the producer client */
    char host[128] = "127.0.0.1";
    int port = PORT;
    char sender_id[128] = "PRODUCER_01";
    char data_val[128] = "42.50";
    int count = 1;
    int delay_ms = 100;
    int abrupt_close = 0;

    /* Parse command-line flags:
     * -h: server IP, -p: server port, -i: producer ID, -d: data value
     * -n: message count, -s: delay between messages (ms), -c: simulate abrupt drop
     */
    int opt;
    while ((opt = getopt(argc, argv, "h:p:i:d:n:s:c")) != -1) {
        switch (opt) {
            case 'h': strncpy(host, optarg, sizeof(host) - 1); break;
            case 'p': port = atoi(optarg); break;
            case 'i': strncpy(sender_id, optarg, sizeof(sender_id) - 1); break;
            case 'd': strncpy(data_val, optarg, sizeof(data_val) - 1); break;
            case 'n': count = atoi(optarg); break;
            case 's': delay_ms = atoi(optarg); break;
            case 'c': abrupt_close = 1; break;
        }
    }

    /* Create TCP socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /* Set target coordinator server address */
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };
    inet_pton(AF_INET, host, &serv_addr.sin_addr);

    /* Connect to the coordinator server */
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect"); safe_close(sockfd); exit(EXIT_FAILURE);
    }

    /* Send configured number of log messages */
    for (int i = 0; i < count; i++) {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s %s\n", sender_id, data_val);
        if (safe_write(sockfd, msg, strlen(msg)) < 0) break;
        if (delay_ms > 0 && i < count - 1) usleep(delay_ms * 1000); /* Pause between sends */
    }

    /* If -c flag is set, simulate sudden crash (RST packet instead of clean TCP FIN) */
    if (abrupt_close) {
        struct linger sl = { .l_onoff = 1, .l_linger = 0 };
        setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
    }

    /* Close connection cleanly */
    safe_close(sockfd);
    return 0;
}
