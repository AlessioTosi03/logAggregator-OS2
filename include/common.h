#ifndef COMMON_H
#define COMMON_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>

#define PORT 8080
#define LOG_FILE "aggregated.log"
#define MAX_LOG_SIZE 1024
#define ALARM_INTERVAL 2
#define BUFFER_SIZE 512

/* EINTR-safe system call wrappers */
ssize_t safe_read(int fd, void *buf, size_t count);
ssize_t safe_write(int fd, const void *buf, size_t count);
int safe_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int safe_close(int fd);

/* Mutual exclusion file locking using fcntl */
int lock_log(int fd);
int unlock_log(int fd);

/* Async-signal-safe timestamp generator & log line formatter */
void get_timestamp_safe(char *buf, size_t size);
void format_log_safe(char *buf, size_t size, const char *ts, const char *id, const char *data);

#endif
