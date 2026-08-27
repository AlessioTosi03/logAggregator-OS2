#ifndef COMMON_H
#define COMMON_H

/* Feature test macros: enable standard POSIX functions */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/* Standard libraries for I/O, networking, threads, and signal handling */
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

/* --- Shared Configuration Settings --- */
#define PORT 8080                 /* Default network port for connections */
#define LOG_FILE "aggregated.log" /* Default output file where all logs are saved */
#define MAX_LOG_SIZE 1024         /* Maximum file size (in bytes) before rotating log */
#define ALARM_INTERVAL 2          /* Periodic check interval (in seconds) */
#define BUFFER_SIZE 512           /* Standard buffer size for network chunks */

/* --- Safe Network & File Operations --- */
/* These wrappers automatically retry if interrupted by an OS signal (EINTR) */
ssize_t safe_read(int fd, void *buf, size_t count);
ssize_t safe_write(int fd, const void *buf, size_t count);
int safe_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int safe_close(int fd);

/* --- File Locking --- */
/* Prevents simultaneous writes from overlapping and corrupting the log file */
int lock_log(int fd);
int unlock_log(int fd);

/* --- Timestamp & Log Formatting --- */
/* Safe time generator and line formatter that avoid crashes during signal handling */
void get_timestamp_safe(char *buf, size_t size);
void format_log_safe(char *buf, size_t size, const char *ts, const char *id, const char *data);

#endif
