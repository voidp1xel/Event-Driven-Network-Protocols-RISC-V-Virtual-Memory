#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "logger.h"

static int logging_enabled = 0;
static FILE *log_file = NULL;

void init_logger(int enable){
    logging_enabled = enable;
    if(enable)
        log_file = fopen("log.txt", "a");
}

void log_event(const char *msg){
    if(!logging_enabled || !log_file) return;

    char time_buffer[30];
    struct timeval tv;
    time_t curtime;

    gettimeofday(&tv, NULL);
    curtime = tv.tv_sec;

    strftime(time_buffer, 30, "%Y-%m-%d %H:%M:%S", localtime(&curtime));
    fprintf(log_file, "[%s.%06ld] [LOG] %s\n", time_buffer, (long)tv.tv_usec, msg);
    fflush(log_file);
}

void close_logger(){
    if(log_file){
        fclose(log_file);
        log_file = NULL;
    }
}