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

int lock_log(int fd) {
    if (fd < 0) return -1;
    struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
    int res;
    do {
        res = fcntl(fd, F_SETLKW, &fl);
    } while (res == -1 && errno == EINTR);
    return res;
}

int unlock_log(int fd) {
    if (fd < 0) return -1;
    struct flock fl = { .l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
    int res;
    do {
        res = fcntl(fd, F_SETLKW, &fl);
    } while (res == -1 && errno == EINTR);
    return res;
}

static void itoa_safe(long val, char *buf) {
    char tmp[32];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    else {
        long v = val < 0 ? -val : val;
        while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
        if (val < 0) tmp[i++] = '-';
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

void get_timestamp_safe(char *buf, size_t size) {
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    char y[16], mo[8], d[8], hr[8], mn[8], sc[8];
    itoa_safe(tm_info.tm_year + 1900, y);
    itoa_safe(tm_info.tm_mon + 1, mo);
    itoa_safe(tm_info.tm_mday, d);
    itoa_safe(tm_info.tm_hour, hr);
    itoa_safe(tm_info.tm_min, mn);
    itoa_safe(tm_info.tm_sec, sc);

    size_t pos = 0;
    #define APPEND(str) for (char *p = (str); *p && pos + 1 < size; p++) buf[pos++] = *p
    #define APPEND_PAD(num_val, str) if ((num_val) < 10 && pos + 1 < size) buf[pos++] = '0'; APPEND(str)

    APPEND(y); if (pos + 1 < size) buf[pos++] = '-';
    APPEND_PAD(tm_info.tm_mon + 1, mo); if (pos + 1 < size) buf[pos++] = '-';
    APPEND_PAD(tm_info.tm_mday, d); if (pos + 1 < size) buf[pos++] = ' ';
    APPEND_PAD(tm_info.tm_hour, hr); if (pos + 1 < size) buf[pos++] = ':';
    APPEND_PAD(tm_info.tm_min, mn); if (pos + 1 < size) buf[pos++] = ':';
    APPEND_PAD(tm_info.tm_sec, sc);
    buf[pos] = '\0';
}

void format_log_safe(char *buf, size_t size, const char *ts, const char *id, const char *data) {
    size_t pos = 0;
    if (pos + 1 < size) buf[pos++] = '[';
    for (const char *p = ts; *p && pos + 1 < size; p++) buf[pos++] = *p;
    if (pos + 3 < size) { buf[pos++] = ','; buf[pos++] = ' '; }
    for (const char *p = id; *p && pos + 1 < size; p++) buf[pos++] = *p;
    if (pos + 3 < size) { buf[pos++] = ',', buf[pos++] = ' '; }
    for (const char *p = data; *p && pos + 1 < size; p++) buf[pos++] = *p;
    if (pos + 2 < size) { buf[pos++] = ']'; buf[pos++] = '\n'; }
    buf[pos] = '\0';
}
