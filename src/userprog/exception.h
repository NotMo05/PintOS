#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/* Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1    /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /* 0: read, 1: write. */
#define PF_U 0x4    /* 0: kernel, 1: user process. */

#define PUSH_SIZE 4
#define PUSHA_SIZE 32
#define STACK_LIMIT (PHYS_BASE - 8 * 1024 * 1024) // 8MB stack limit

void exception_init (void);
void exception_print_stats (void);

#endif /* userprog/exception.h */
