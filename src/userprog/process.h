#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#define MAX_ARGS 128

#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "lib/kernel/hash.h"

struct lock filesystem_lock;

struct file_descriptor {
    int fd;
    struct file *file;
    struct hash_elem hash_elem;
};

// Struct to help query between a parent process and its child process
struct child_info {
    int child_tid;  /* child thread id */
    int parent_tid; /* parent thread id */
    int accesses;
    struct lock access_lock;
    int exit_status; /* child's exit status */
    bool waited; /* Was process_wait called flag */
    bool load_success; /* Was load() successful flag */
    struct semaphore wait_sema; /* process_wait sema */
    struct semaphore load_wait;
    struct list_elem elem; /* list elem for children_list */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool install_page (void *upage, void *kpage, bool writable);

#endif /* userprog/process.h */
