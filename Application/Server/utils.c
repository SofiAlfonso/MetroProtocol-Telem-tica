//
// Created by Ana Sofia Alfonso Moncada on 21/09/25.
//

#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>

FILE *logfile = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm);

    pthread_mutex_lock(&log_mutex);

    printf("[%s] ", timestr);
    vprintf(fmt, ap);
    printf("\n");

    if (logfile) {
        va_list ap2;
        va_copy(ap2, ap);
        fprintf(logfile, "[%s] ", timestr);
        vfprintf(logfile, fmt, ap2);
        fprintf(logfile, "\n");
        fflush(logfile);
        va_end(ap2);
    }
    pthread_mutex_unlock(&log_mutex);

    va_end(ap);
}
