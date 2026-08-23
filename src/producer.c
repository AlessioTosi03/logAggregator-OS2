#include "common.h"

int main(int argc, char *argv[]) {
    char host[128] = "127.0.0.1";
    int port = PORT;
    char sender_id[128] = "PRODUCER_01";
    char data_val[128] = "42.50";
    int count = 1;
    int delay_ms = 100;
    int abrupt_close = 0;

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

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };
    inet_pton(AF_INET, host, &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect"); safe_close(sockfd); exit(EXIT_FAILURE);
    }

    for (int i = 0; i < count; i++) {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s %s\n", sender_id, data_val);
        if (safe_write(sockfd, msg, strlen(msg)) < 0) break;
        if (delay_ms > 0 && i < count - 1) usleep(delay_ms * 1000);
    }

    if (abrupt_close) {
        struct linger sl = { .l_onoff = 1, .l_linger = 0 };
        setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
    }

    safe_close(sockfd);
    return 0;
}
