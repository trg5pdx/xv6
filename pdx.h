/*
 * This file contains types and definitions for Portland State University.
 * The contents are intended to be visible in both user and kernel space.
 */

#ifndef PDX_INCLUDE
#define PDX_INCLUDE

#define TRUE 1
#define FALSE 0
#define RETURN_SUCCESS 0
#define RETURN_FAILURE -1

#define NUL 0
#ifndef NULL
#define NULL NUL
#endif  // NULL

#define TPS 1000   // ticks-per-second
#define SCHED_INTERVAL (TPS/100)  // see trap.c

#define NPROC  64  // maximum number of processes -- normally in param.h

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#ifdef CS333_P2
#define DEF_UID 1
#define DEF_GID 1
#define PROCSIZE 16 // Size used by uproc process array; different from NPROC for testing purposes
#endif // CS333_P2

#ifdef CS333_P4
#define MAXPRIO 6
#define TICKS_TO_PROMOTE 3000
#define DEFAULT_BUDGET 300
#endif // CS333_P4

#ifdef CS333_P5
// DEFAULT_UID is the default value for both the first process and files
// created by mkfs when the file system is created
#define DEFAULT_UID 0
#define DEFAULT_GID 0
// DEFAULT_MODE group of defines set the default permissions for processes & files, usually set as 755
#define DEFAULT_MODE 0755
#endif // CS333_P5

#endif  // PDX_INCLUDE
