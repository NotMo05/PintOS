#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <hash.h>
#include "userprog/syscall.h"

struct mmap_entry {
  mapid_t mapid;              /* id for the mapping */
  struct file* file;          /* file being mapped */
  void *start_addr;           /* starting virtual user address of the mapping */
  size_t length;              /* length of the mapping in bytes */
  struct hash_elem hash_elem; /* hash_elem for the mmap_entry for the mmap hash
                                 table in the thread */
};

unsigned mmap_hash(const struct hash_elem *p_hash_elem, void *aux UNUSED);
bool mmap_less(const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED);

#endif