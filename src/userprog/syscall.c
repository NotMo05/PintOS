#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <round.h>
#include <inttypes.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "../devices/shutdown.h"
#include "../filesys/filesys.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "pagedir.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "userprog/exception.h"
#include "vm/mmap.h"
#include "filesys/off_t.h"

static void syscall_handler (struct intr_frame *);
static bool valid_user_pointer(const void *uaddr);
static bool valid_user_buffer(const void *buffer, unsigned size);
typedef void (*syscall_func)(struct intr_frame *);

syscall_func syscall_table[TOTAL_SYSCALL_NO];

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  syscall_table[SYS_HALT] = handle_halt;
  syscall_table[SYS_EXIT] = handle_exit;
  syscall_table[SYS_EXEC] = handle_exec;
  syscall_table[SYS_WAIT] = handle_wait;
  syscall_table[SYS_CREATE] = handle_create;
  syscall_table[SYS_REMOVE] = handle_remove;
  syscall_table[SYS_OPEN] = handle_open;
  syscall_table[SYS_FILESIZE] = handle_filesize;
  syscall_table[SYS_READ] = handle_read;
  syscall_table[SYS_WRITE] = handle_write;
  syscall_table[SYS_SEEK] = handle_seek;
  syscall_table[SYS_TELL] = handle_tell;
  syscall_table[SYS_CLOSE] = handle_close;
  syscall_table[SYS_MMAP] = handle_mmap;
  syscall_table[SYS_MUNMAP] = handle_munmap;

  lock_init(&filesystem_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  if (!valid_user_pointer(f->esp)) {
    exit(-1);
  }
  int syscall_number = *(int *)(f->esp);  // Get syscall number from stack pointer
  if (syscall_number < 0 || syscall_number >= TOTAL_SYSCALL_NO || syscall_table[syscall_number] == NULL) {
      exit(-1); // Invalid syscall number
    } else {
      syscall_table[syscall_number](f);  // Dispatch to the appropriate handler
    }
}

void handle_halt(struct intr_frame *f UNUSED) {
  halt();
}

void halt(void) {
  shutdown_power_off();
}

void handle_exit(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  int status = *(int *)(f->esp + 4);  // Retrieve status from stack
  exit(status);  // Call exit with status
}

void exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_current()->my_info->exit_status = status;
  thread_exit();
}

void handle_exec(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  const char *cmd_line = *(const char **)(f->esp + 4);
  if (!valid_user_pointer(cmd_line)) {
    exit(-1);
  }
  f->eax = exec(cmd_line);
}

pid_t exec(const char *cmd_line) {
  return process_execute(cmd_line);
}

void handle_wait(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  pid_t pid = *(pid_t *)(f->esp + 4);
  f->eax = wait(pid);
}

int wait(pid_t pid) {
  return process_wait(pid);
}

void handle_write(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4) || !valid_user_pointer(f->esp + 8) || !valid_user_pointer(f->esp + 12)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);                      // Retrieve fd from stack
  const void *buffer = *(const void **)(f->esp + 8);  // Retrieve buffer pointer from stack
  unsigned size = *(unsigned *)(f->esp + 12);         // Retrieve size from stack
  if (fd > MAX_FILES_OPEN || !valid_user_buffer(buffer, size)) {
    exit(-1);
  }
  lock_acquire(&filesystem_lock);
  f->eax = write(fd, buffer, size);  // Call write and store the result in f->eax
  lock_release(&filesystem_lock);
}

int write(int fd, const void *buffer, unsigned size) {
  if (buffer == NULL) {
    return -1;  // Return -1 if buffer is NULL
  }
  if (fd == 1) {
    // Write to the console
    putbuf((const char *)buffer, size);  // Write the entire buffer to the console
    return size;  // Return the full size since console output does not partial write
  } else {
    // Write to file
    struct file *file = get_file_by_fd(fd);
    if (file == NULL) {
      return -1;
    }
    return file_write(file, buffer, size);
  }
}

void handle_open(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  const char *filename = *(const char **)(f->esp + 4);
  if (!valid_user_pointer(filename)) {
    exit(-1);
  }
  lock_acquire(&filesystem_lock);
  f->eax = open(filename);
  lock_release(&filesystem_lock);
}

int open(const char *filename) {
  struct file *file = filesys_open(filename);
  if (file == NULL) {
    return -1; // could not open file
  }
  struct file_descriptor *file_descriptor = malloc(sizeof(struct file_descriptor));
  if (file_descriptor == NULL) {
    file_close(file);
    return -1;
  }
  struct thread *cur = thread_current();
  file_descriptor->file = file;
  file_descriptor->fd = cur->next_fd++;
  hash_insert(&cur->fd_hash_table, &file_descriptor->hash_elem);
  return file_descriptor->fd;
}

void handle_read(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4) || !valid_user_pointer(f->esp + 8) || !valid_user_pointer(f->esp + 12)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);                      // Retrieve fd from stack
  void *buffer = *(void **)(f->esp + 8);  // Retrieve buffer pointer from stack
  unsigned size = *(unsigned *)(f->esp + 12);         // Retrieve size from stack
  if (fd > MAX_FILES_OPEN || !valid_user_buffer(buffer, size)) {
    exit(-1);
  }
  lock_acquire(&filesystem_lock);
  f->eax = read(fd, buffer, size);
  lock_release(&filesystem_lock);
}

int read(int fd, void *buffer, unsigned size) {
  if (buffer == NULL) {
    return -1;  // Return -1 if buffer is NULL
  }
  if (fd == 0) {
    // Read from keyboard
    char *charbuf = (char *)buffer;
    for (unsigned i = 0; i < size; i++) {
      charbuf[i] = input_getc();
    }
    return size;
  } else {
    // Read from file
    struct file *file = get_file_by_fd(fd);
    if (file == NULL) {
      return -1;
    }
    return file_read(file, buffer, size);
  }
}

void handle_filesize(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);
  lock_acquire(&filesystem_lock);
  f->eax = filesize(fd);
  lock_release(&filesystem_lock);
}

int filesize(int fd) {
  struct file *file = get_file_by_fd(fd);
  if (file == NULL) {
    return -1;
  }
  return file_length(file);
}

void handle_create(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4) || !valid_user_pointer(f->esp + 8)) {
    exit(-1);
  }
  const char *filename = *(const char **)(f->esp + 4);
  unsigned initial_size = *(unsigned *)(f->esp + 8);
  if (!valid_user_pointer(filename)) {
    exit(-1);
  }
  lock_acquire(&filesystem_lock);
  f->eax = create(filename, initial_size);
  lock_release(&filesystem_lock);
}

bool create(const char *filename, unsigned initial_size) {
  return filesys_create(filename, initial_size);
}

void handle_remove(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  const char *filename = *(const char **)(f->esp + 4);
  if (!valid_user_pointer(filename)) {
    exit(-1);
  }
  lock_acquire(&filesystem_lock);
  f->eax = remove(filename);
  lock_release(&filesystem_lock);
}

bool remove(const char *filename) {
  return filesys_remove(filename);
}

void handle_seek(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4) || !valid_user_pointer(f->esp + 8)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);
  unsigned position = *(unsigned *)(f->esp + 8);
  lock_acquire(&filesystem_lock);
  seek(fd, position);
  lock_release(&filesystem_lock);
}

void seek(int fd, unsigned position) {
  struct file *file = get_file_by_fd(fd);
  file_seek(file, position);
}

void handle_tell(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);
  lock_acquire(&filesystem_lock);
  f->eax = tell(fd);
  lock_release(&filesystem_lock);
}

unsigned tell(int fd) {
  struct file *file = get_file_by_fd(fd);
  return file_tell(file);
}

void handle_close(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  int fd = *(int *)(f->esp + 4);
  lock_acquire(&filesystem_lock);
  close(fd);
  lock_release(&filesystem_lock);
}

void close(int fd) {
  struct thread *cur = thread_current();
  struct file_descriptor *file_descriptor = fd_lookup(fd);
  if (file_descriptor == NULL) {
    return;
  }
  file_close(file_descriptor->file);
  hash_delete(&cur->fd_hash_table, &file_descriptor->hash_elem);
  free(file_descriptor);
}

void handle_mmap(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4) || !valid_user_pointer(f->esp + 8)) {
    exit(-1);
  }


  int fd = *(int *)(f->esp + 4);
  void *addr = *(void **)(f->esp + 8);
  lock_acquire(&filesystem_lock);
  f->eax = mmap(fd, addr);
  lock_release(&filesystem_lock);
}

// Maps a file into memory
mapid_t mmap(int fd, void *addr) {


  struct thread *cur = thread_current();
  ASSERT(cur != NULL);

  struct file_descriptor *file_descriptor = fd_lookup(fd);


  if (file_descriptor == NULL) {
    return -1; // Failed to mmap, invalid fd
  }

  struct file *file = file_descriptor->file;

  ASSERT(file != NULL); // A file_descriptor should have a valid file

  if (file_length(file) == 0         // opened file should not be 0 bytes long
      || addr != pg_round_down(addr) // addr must be page aligned
      || addr == NULL                // addr can't be NULL
      || fd == STDIN_FILENO          // Cannot map console input/output
      || fd == STDOUT_FILENO) {
    return -1; // Failed to mmap
  }

  struct file *m_file = file_reopen(file);
  if (m_file == NULL) {
    return -1; // Failed to mmap, could not reopen the file
  }

  int length = file_length(m_file);
  int num_pages = DIV_ROUND_UP(length, PGSIZE);
  void *cur_addr = addr;

  for (int i = 0; i < num_pages; i++) {
    if (spt_lookup(cur_addr, &cur->spt) != NULL  // Holds if memory already mapped
        || cur_addr >= STACK_LIMIT) { /* Holds if we are in stack space or kernel
                                         space */
      file_close(m_file);
      return -1; // Failed to mmap
    }
    cur_addr += PGSIZE; // Increment and check the next page for validity
  }

  struct mmap_entry *mmap = malloc(sizeof(struct mmap_entry));
  if (mmap == NULL) {
    file_close(m_file);
    return -1; // Failed to mmap, could not malloc
  }

  // Intialising mmap data
  mmap->mapid = cur->next_mapid++;
  mmap->file = m_file;
  mmap->start_addr = addr;
  mmap->length = length;
  hash_insert(&cur->mmap_hash_table, &mmap->hash_elem);

  cur_addr = addr;
  off_t offset = 0;
  // Generating spt page entries for later lazy-loading
  while (length > 0) {
    size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct page *page = malloc(sizeof(struct page));
    if (page == NULL) {
      munmap(mmap->mapid);
      return -1; // Failed to mmap, could not malloc
    }

    page->vaddr = cur_addr;
    page->status = PAGE_FILE;
    page->file = m_file;
    page->writable = true;
    page->read_bytes = page_read_bytes;
    page->zero_bytes = page_zero_bytes;
    page->file_offset = offset;
    page->frame = NULL;
    page->swapped = false;

    lock_acquire(&spt_lock);

    hash_insert(&cur->spt, &page->hash_elem);

    lock_release(&spt_lock);

    length -= page_read_bytes;
    cur_addr += PGSIZE;
    offset += page_read_bytes;
  }

  return mmap->mapid;
}

void handle_munmap(struct intr_frame *f) {
  if (!valid_user_pointer(f->esp + 4)) {
    exit(-1);
  }
  mapid_t mapping = *(mapid_t *)(f->esp + 4);
  lock_acquire(&filesystem_lock);
  munmap(mapping);
  lock_release(&filesystem_lock);
}

// Helper function for unmapping pages in a map from memory
static struct mmap_entry *unmap(struct hash_elem *h_e, struct thread *cur) {

  struct mmap_entry *mmap = hash_entry(h_e, struct mmap_entry, hash_elem);
  ASSERT(mmap != NULL);

  void *cur_addr = mmap->start_addr;
  size_t rem_length = mmap->length;

  while (rem_length > 0) {
    struct page *page = spt_lookup(cur_addr, &cur->spt);
    if (page != NULL) {
      void *kernel_alias_addr = pagedir_get_page(cur->pagedir, cur_addr);
      if (pagedir_is_dirty(cur->pagedir, cur_addr)) {
        if (file_write_at(mmap->file, cur_addr, page->read_bytes, page->file_offset) != page->read_bytes) {
          printf("Failed to write back to file during munmap\n");
        }
      }

      pagedir_set_dirty(cur->pagedir, cur_addr, false);
      pagedir_set_accessed(cur->pagedir, cur_addr, false);
      pagedir_set_dirty(cur->pagedir, kernel_alias_addr, false);
      pagedir_set_accessed(cur->pagedir, kernel_alias_addr, false);

      pagedir_clear_page(cur->pagedir, cur_addr);

      lock_acquire(&spt_lock);

      hash_delete(&cur->spt, &page->hash_elem);

      lock_release(&spt_lock);

      free(page);
    }

    size_t decrement = rem_length < PGSIZE ? rem_length : PGSIZE;
    rem_length -= decrement;
    cur_addr += PGSIZE;
  }

  file_close(mmap->file);
  return mmap;
}

// Action function to unmap and free a map from memory
void mmap_free(struct hash_elem *e, void *aux UNUSED) {

  struct thread *cur = thread_current();
  // Unmapping the map
  struct mmap_entry *mmap = unmap(e, cur);
  // Freeing the map
  free(mmap);
}

/* Unmaps a mapping, deletes it from the memory mapping table, and frees it from
   memory */
void munmap(mapid_t mapping) {

  // Finding the mmap corresponding to mapping
  struct mmap_entry temp_mmap;
  temp_mmap.mapid = mapping;
  struct thread *cur = thread_current();

  struct hash_elem *h_e = hash_find(&cur->mmap_hash_table, &temp_mmap.hash_elem);
  ASSERT(h_e != NULL)
  // Unmapping
  struct mmap_entry *mmap = unmap(h_e, cur);
  // Deleting from memory mapping table
  hash_delete(&cur->mmap_hash_table, &mmap->hash_elem);
  // Freeing the map
  free(mmap);

  return;
}

// Helper function to lookup into fd_hash_table by fd
struct file_descriptor* fd_lookup(int fd) {
  if (fd < 2) {
    return NULL;
  }
  struct thread *cur = thread_current();
  struct file_descriptor file_descriptor;
  file_descriptor.fd = fd;
  struct hash_elem *e = hash_find(&cur->fd_hash_table, &file_descriptor.hash_elem);
  return e != NULL ? hash_entry(e, struct file_descriptor, hash_elem) : NULL;
}



// Helper function to get file by fd
struct file* get_file_by_fd(int fd) {
  struct file_descriptor *file_descriptor = fd_lookup(fd);
  return file_descriptor != NULL ? file_descriptor->file : NULL;
}

// Helper function for pointer validation
static bool valid_user_pointer(const void *uaddr) {
  return uaddr != NULL && is_user_vaddr(uaddr);
}

// Helper function for user buffer validation
static bool valid_user_buffer(const void *buffer, unsigned size) {
  for (unsigned i = 0; i < size; i += PGSIZE) {
    if (!valid_user_pointer(buffer + i)) {
      return false;
    }
  }
  return valid_user_pointer(buffer + size);
}
