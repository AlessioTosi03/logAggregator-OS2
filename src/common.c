#include "common.h"

ssize_t safe_read(int fd, void *buf, size_t count) {
    ssize_t n;
    do {
        n = read(fd, buf, count);
    } while (n == -1 && errno == EINTR);
    return n;
}

ssize_t safe_write(int fd, const void *buf, size_t count) {
    size_t written = 0;
    const char *ptr = (const char *)buf;
    while (written < count) {
        ssize_t n = write(fd, ptr + written, count - written);
        if (n == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        written += n;
    }
    return written;
}

int safe_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int fd;
    do {
        fd = accept(sockfd, addr, addrlen);
    } while (fd == -1 && errno == EINTR);
    return fd;
}

int safe_close(int fd) {
    if (fd < 0) return 0;
    int res;
    do {
        res = close(fd);
    } while (res == -1 && errno == EINTR);
    return res;
}
