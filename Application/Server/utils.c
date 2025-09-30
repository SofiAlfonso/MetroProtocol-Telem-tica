// utils.c
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>

FILE *logfile = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_msg(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap); // copiar antes de usar ap

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm);

    pthread_mutex_lock(&log_mutex);

    // imprimir en stdout
    printf("[%s] ", timestr);
    vprintf(fmt, ap);
    printf("\n");

    // imprimir en logfile si existe
    if (logfile) {
        fprintf(logfile, "[%s] ", timestr);
        vfprintf(logfile, fmt, ap2);
        fprintf(logfile, "\n");
        fflush(logfile);
    }

    pthread_mutex_unlock(&log_mutex);

    va_end(ap2);
    va_end(ap);
}
