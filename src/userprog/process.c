#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/serial.h"
#include "syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/mmap.h"

#define MAX_STR_LENGTH 1024

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static unsigned fd_hash(const struct hash_elem *p_, void *aux UNUSED);
static bool fd_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. The 
   file_name given includes all the arguments for the process. */
tid_t
process_execute (const char *file_name) 
{
  // Copy of the process + argument string
  char *fn_copy;
  /* Another copy of the process + argument string (needed as this will get
     modified when calling strtok_r on it and fn_copy needs to be passed intact
     to start_process so that it also has the process + arguments) */ 
  char *fn_arg_copy;
  tid_t tid;

  // Set a reasonable limit on the length of command line arguments
  if (strnlen(file_name, MAX_STR_LENGTH) == MAX_STR_LENGTH) {
    return TID_ERROR;
  }
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load() when start_process
     is called upon the scheduling of the newly created thread which would
     modify file name due to strtok_r. */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  fn_arg_copy = palloc_get_page (0);
  if (fn_arg_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_arg_copy, file_name, PGSIZE);

  char *save_ptr;
  // Get just the program name from the program + arg string
  char *program_name = strtok_r(fn_arg_copy, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (program_name, PRI_DEFAULT, start_process, fn_copy);

  // Freeing fn_arg_copy right after usage
  palloc_free_page(fn_arg_copy);

  if (tid == TID_ERROR) {
    palloc_free_page (fn_copy);
    return tid;
  }
  /* Creating a child_info struct to allow querying between parent and child
     processes */
  struct thread *cur = thread_current();
  enum intr_level old_level = intr_disable();
  struct thread *child = get_thread_by_tid(tid);
  intr_set_level(old_level);

  // child_info allocation and init code
  struct child_info *child_info = malloc(sizeof(struct child_info));
  if (child_info == NULL) {
    return TID_ERROR;
  }

  // Init child_info
  child_info->child_tid = tid;
  child_info->parent_tid = cur->tid;
  child_info->exit_status = INIT_EXIT_STATUS;
  sema_init(&child_info->load_wait, 0);
  lock_init(&child_info->access_lock);
  child_info->accesses = 0;
  child_info->load_success = true;
  child_info->waited = false;
  sema_init(&child_info->wait_sema, 0);
  list_push_back(&cur->children_list, &child_info->elem);
  child->my_info = child_info;

  // Waiting for load to finish
  sema_down(&child_info->load_wait);
  if (!child_info->load_success) {
    // If failed, free just allocated child_info to not leak resources
    list_remove(&child_info->elem);
    free(child_info);
    return TID_ERROR;
  }

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  struct thread *cur = thread_current();
  hash_init(&cur->fd_hash_table, fd_hash, fd_less, NULL);
  hash_init(&cur->spt, spt_hash, spt_less, NULL);
  hash_init(&cur->mmap_hash_table, mmap_hash, mmap_less, NULL);
  
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* Separating the file_name string into arguments. We keep track of the
     number of args via argc */ 
  char *argv[MAX_ARGS];
  int argc = 0;
  char *save_ptr;
  for (char *arg = strtok_r(file_name, " ", &save_ptr); arg != NULL; arg = strtok_r(NULL, " ", &save_ptr)) {
    if (argc == MAX_ARGS) {
      break;
    }
    argv[argc] = arg;
    argc++;
  }

  /* Loading the program corresponding to the process name and storing its 
     initial stack pointer and entry point into esp and eip in the frame. */
  lock_acquire(&filesystem_lock);
  success = load (argv[0], &if_.eip, &if_.esp);
  lock_release(&filesystem_lock);

  // Sending load result to process_execute
  cur->my_info->load_success = success;
  sema_up(&cur->my_info->load_wait);

  if (success) {
    // ARGUMENT PASSING SEGMENT
    // Pushing the arguments onto stack in reverse order
    void *esp = if_.esp;
    char *arg_addresses[argc];
    for (int i = argc - 1; i >= 0; i--) {
      esp -= strlen(argv[i]) + 1;
      memcpy(esp, argv[i], strlen(argv[i]) + 1); // Copy ith arg to esp
      arg_addresses[i] = esp; // Store ith arg's address
    }
    /* Word-align esp by 4 bytes. */ 
    esp = (void *)((uintptr_t)esp & 0xfffffffc); 
    // Push NULL for argv[argc]
    esp -= sizeof(char *);
    *((char **) esp) = NULL;
    // Push arg addresses
    for (int i = argc - 1; i >= 0; i--) {
      esp -= sizeof(char *);
      *((char **) esp) = arg_addresses[i];
    }
    // Push argv pointer to stack
    char **argv_ptr = esp;
    esp -= sizeof(char **);
    *(char ***)esp = argv_ptr;
    // Push argc
    esp -= sizeof(int);
    *(int *)esp = argc;
    // Push fake return address
    esp -= sizeof(void *);
    *(void **)esp = NULL;
    // Updating the stack pointer of the user process
    if_.esp = esp;
  }
  palloc_free_page (file_name);
  /* If load failed, quit. */
  if (!success) {
    thread_exit ();
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status. 
 * If it was terminated by the kernel (i.e. killed due to an exception), 
 * returns -1.  
 * If TID is invalid or if it was not a child of the calling process, or if 
 * process_wait() has already been successfully called for the given TID, 
 * returns -1 immediately, without waiting.
 *
 * This function was implemented in task 2. */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct thread *cur = thread_current();
  for (struct list_elem *e = list_begin(&cur->children_list); e != list_end(&cur->children_list); e = list_next(e)) {
    struct child_info *child_info = list_entry(e, struct child_info, elem);
    if (child_info->child_tid == child_tid && !child_info->waited) {
      child_info->waited = true;

      sema_down(&child_info->wait_sema);

      list_remove(&child_info->elem);
      int status = child_info->exit_status;
      lock_acquire(&child_info->access_lock);
      child_info->accesses++;
      if (child_info->accesses == 2) {
        lock_release(&child_info->access_lock);
        free(child_info);
      } else {
        lock_release(&child_info->access_lock);
      }
      return status;
    }
  }
  return -1; // tid is invalid / not child of this process
}

/* Function for free'ing file_descriptor's in hash_destroy */
static void fd_free(struct hash_elem *e, void *aux UNUSED) {
  struct file_descriptor *file_descriptor = hash_entry(e, struct file_descriptor, hash_elem);
  file_close(file_descriptor->file);
  free(file_descriptor);
}

/* Function for getting hash for file_descriptor */
static unsigned fd_hash(const struct hash_elem *p_, void *aux UNUSED) {
  const struct file_descriptor *fd = hash_entry(p_, struct file_descriptor, hash_elem);
  return hash_int(fd->fd);
}

/* Function for hash ordering for file_descriptor */
static bool fd_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  const struct file_descriptor *a = hash_entry(a_, struct file_descriptor, hash_elem);
  const struct file_descriptor *b = hash_entry(b_, struct file_descriptor, hash_elem);
  return a->fd < b->fd;
}
/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  // Reenable writes and close the executable
  if (cur->executable != NULL) {
    lock_acquire(&filesystem_lock);
    file_close(cur->executable); // Automatically reenables writes
    lock_release(&filesystem_lock);
  }

  lock_acquire(&filesystem_lock);
  hash_destroy(&cur->mmap_hash_table, mmap_free);
  lock_release(&filesystem_lock);
  
  // Destroy hash and dealloc all resources used
  lock_acquire(&filesystem_lock);
  hash_destroy(&cur->fd_hash_table, fd_free);
  lock_release(&filesystem_lock);

  // Pass signal via semaphore to parent's process_wait
  struct child_info *info = cur->my_info;
  if (info != NULL) {
    sema_up(&info->wait_sema);
    lock_acquire(&info->access_lock);
    info->accesses++;
    if (info->accesses == 2) {
      lock_release(&info->access_lock);
      free(info);
    } else {
      lock_release(&info->access_lock);
    }
  }

  // Free unfreed child_info structs left
  struct list_elem *e = list_begin(&cur->children_list);
  while (e != list_end(&cur->children_list)) {
    struct child_info *child_info = list_entry(e, struct child_info, elem);
    e = list_remove(&child_info->elem);
    lock_acquire(&child_info->access_lock);
    child_info->accesses++;
    if (child_info->accesses == 2) {
      lock_release(&child_info->access_lock);
      free(child_info);
    } else {
      lock_release(&child_info->access_lock);
    }
  }
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
      lock_acquire(&spt_lock);
      hash_destroy(&cur->spt, spt_free);
      lock_release(&spt_lock);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  file_deny_write(file);
  thread_current()->executable = file;

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
//  file_close (file);
  return success;
}

/* load() helpers. */

bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  struct thread *cur = thread_current();
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct page *page = malloc(sizeof(struct page));
      if (page == NULL) {
        return false;
      }
      page->vaddr = upage;
      page->status = PAGE_FILE;
      page->writable = writable;
      page->file = file;
      page->read_bytes = page_read_bytes;
      page->zero_bytes = page_zero_bytes;
      page->file_offset = ofs;
      page->frame = NULL;
      page->swapped = false;

      hash_insert(&cur->spt, &page->hash_elem);

      // Simulate advancing by file_read
      ofs += page_read_bytes;
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  void *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;

  struct frame_entry *f_entry = frame_alloc((PAL_USER | PAL_ZERO), upage);
  kpage = f_entry->frame_addr;
  struct page *page = malloc(sizeof (struct page));
  page->vaddr = f_entry->upage_addr;
  page->frame = f_entry;
  page->status = PAGE_STACK;
  page->swapped = false;
  page->writable = true;

  hash_insert(&thread_current()->spt, &page->hash_elem);

  if (kpage != NULL) 
    {
      success = install_page (upage, kpage, true);
      if (success)
        *esp = PHYS_BASE - 12;
      else
        frame_free (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
