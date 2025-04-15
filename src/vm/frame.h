#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/synch.h"
#include "threads/palloc.h"


struct hash frame_hash_table;
struct lock frame_table_lock;

struct frame_entry {
  void *frame_addr; /* Will actually be kernel virtual addresses with 1-1 
                       mapping to RAM.*/ 
  void *upage_addr; // user virtual page the frame maps to
  struct thread *owner; // Thread that owns the frame
  struct hash_elem hash_elem; // hash_elem for this frame table hash entry
};

unsigned frame_hash(const struct hash_elem *p_hash_elem, void *aux UNUSED);
bool frame_less(const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED);
struct frame_entry *frame_evict(void *upage);
struct frame_entry *frame_alloc(enum palloc_flags flags, void *upage);
void frame_free(void *frame);

#endif