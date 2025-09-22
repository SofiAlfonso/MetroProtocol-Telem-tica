//
// Created by Ana Sofia Alfonso Moncada on 21/09/25.
//

#ifndef METROPROTOCOL_UTILS_H
#define METROPROTOCOL_UTILS_H
void log_msg(const char *fmt, ...);
void handle_sigint(int sign);

extern FILE *logfile;
#endif //METROPROTOCOL_UTILS_H