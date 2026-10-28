#include "sylib.h"
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>

// Local validation runtime, independently implemented from the published ABI.
// Timing is diagnostic output on stderr, never program stdout.
typedef struct TimerRecord {
    int start_line, stop_line;
    int64_t microseconds;
} TimerRecord;
static struct timeval timer_started;
static TimerRecord* timer_records;
static size_t timer_count, timer_capacity;
static int timer_start_line, timer_active, timer_registered;
static int64_t timer_total;
static void print_duration(FILE* stream, int64_t elapsed) {
    fprintf(stream, "%lldH-%lldM-%lldS-%lldus\n",
        (long long)(elapsed / 3600000000LL),
        (long long)(elapsed / 60000000LL % 60),
        (long long)(elapsed / 1000000LL % 60),
        (long long)(elapsed % 1000000LL));
}
static void report_timers(void) {
    for (size_t i=0;i<timer_count;++i) {
        fprintf(stderr, "Timer#%03zu@%04d-%04d: ", i+1,
                timer_records[i].start_line, timer_records[i].stop_line);
        print_duration(stderr, timer_records[i].microseconds);
    }
    if (timer_count) { fputs("TOTAL: ", stderr); print_duration(stderr, timer_total); }
    free(timer_records);
}

int getint() {
    int t;
    if (scanf("%d", &t) != 1) return 0;
    return t;
}

int getch() {
    return getchar();
}

float getfloat() {
    float n;
    if (scanf("%a", &n) != 1) { // try hex float or standard float
        if (scanf("%f", &n) != 1) return 0.0f;
    }
    return n;
}

int getarray(int a[]) {
    int n;
    if (scanf("%d", &n) != 1) return 0;
    for (int i = 0; i < n; i++) {
        if (scanf("%d", &a[i]) != 1) break;
    }
    return n;
}

int getfarray(float a[]) {
    int n;
    if (scanf("%d", &n) != 1) return 0;
    for (int i = 0; i < n; i++) {
        if (scanf("%a", &a[i]) != 1) {
            if (scanf("%f", &a[i]) != 1) break;
        }
    }
    return n;
}

void putint(int a) {
    printf("%d", a);
}

void putch(int a) {
    printf("%c", (char)a);
}

void putfloat(float a) {
    printf("%a", a);
}

void putarray(int n, int a[]) {
    printf("%d:", n);
    for (int i = 0; i < n; i++) {
        printf(" %d", a[i]);
    }
    printf("\n");
}

void putfarray(int n, float a[]) {
    printf("%d:", n);
    for (int i = 0; i < n; i++) {
        printf(" %a", a[i]);
    }
    printf("\n");
}

void putf(char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void _sysy_starttime(int line) {
    if (!timer_registered) { atexit(report_timers); timer_registered=1; }
    timer_start_line=line;
    timer_active=1;
    gettimeofday(&timer_started, NULL);
}

void _sysy_stoptime(int line) {
    struct timeval stopped;
    if (!timer_active) return;
    gettimeofday(&stopped, NULL);
    timer_active=0;
    int64_t elapsed=(int64_t)(stopped.tv_sec-timer_started.tv_sec)*1000000
                   +stopped.tv_usec-timer_started.tv_usec;
    if (elapsed<0) elapsed=0;
    if (timer_count==timer_capacity) {
        size_t capacity=timer_capacity ? timer_capacity*2 : 16;
        TimerRecord* records=(TimerRecord*)realloc(timer_records,capacity*sizeof(*records));
        if (!records) return;
        timer_records=records;timer_capacity=capacity;
    }
    timer_records[timer_count++]=(TimerRecord){timer_start_line,line,elapsed};
    timer_total+=elapsed;
}
