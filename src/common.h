#ifndef COMMON_H
#define COMMON_H

#include <stdarg.h>

/* Global verbose flag - defined in main.c */
extern int verbose;

/* Verbose logging function - prints to stderr only if verbose is true */
void vlog(const char *format, ...);

#endif /* COMMON_H */
