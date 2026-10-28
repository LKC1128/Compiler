#ifndef __SYLIB_H__
#define __SYLIB_H__

#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

/* Input functions */
int getint(void);
int getch(void);
float getfloat(void);
int getarray(int a[]);
int getfarray(float a[]);

/* Output functions */
void putint(int a);
void putch(int a);
void putfloat(float a);
void putarray(int n, int a[]);
void putfarray(int n, float a[]);
void putf(char *format, ...);

/* Timing functions */
void _sysy_starttime(int line);
void _sysy_stoptime(int line);
#define starttime() _sysy_starttime(__LINE__)
#define stoptime() _sysy_stoptime(__LINE__)

#endif // __SYLIB_H__
