#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#define MAX_FILES_OPEN 128
#define TOTAL_SYSCALL_NO 20

#include "threads/interrupt.h"  // Provides full definition of struct intr_frame
#include "list.h"
#include "filesys/file.h"
#include "process.h"
#include "user/syscall.h"

void syscall_init(void);

void handle_halt(struct intr_frame *f);
void handle_exit(struct intr_frame *f);
void handle_exec(struct intr_frame *f);
void handle_wait(struct intr_frame *f);
void handle_write(struct intr_frame *f);
void handle_open(struct intr_frame *f);
void handle_read(struct intr_frame *f);
void handle_filesize(struct intr_frame *f);
void handle_create(struct intr_frame *f);
void handle_remove(struct intr_frame *f);
void handle_seek(struct intr_frame *f);
void handle_tell(struct intr_frame *f);
void handle_close(struct intr_frame *f);
void handle_mmap(struct intr_frame *f);
void handle_munmap(struct intr_frame *f);
void mmap_free(struct hash_elem *e, void *aux UNUSED);

struct file_descriptor* fd_lookup(int fd);
struct file* get_file_by_fd(int fd);

#endif /* userprog/syscall.h */
