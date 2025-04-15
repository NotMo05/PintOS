#ifndef PINTOS_47_PAGE_H
#define PINTOS_47_PAGE_H

#include <debug.h>
#include <hash.h>
#include "devices/swap.h"
#include "vm/frame.h"
#include "threads/thread.h"
#include "filesys/off_t.h"

struct lock spt_lock;

enum page_status {
    PAGE_FILE, // Page backed by a file
    PAGE_STACK, // Page allocated for stack
};

// Supplemental page table entry
struct page {
  void *vaddr; // user virtual address
  enum page_status status; // status of the page
  struct frame_entry *frame; // frame of this page
  struct file *file; // File to load page from
  void *kaddr;
  size_t swap_slot; // swap slot of the page
  bool swapped;
  bool writable; // Writeable flag(lazy load)
  int read_bytes; // read bytes(lazy load)
  int zero_bytes; // zero bytes(lazy load)
  off_t file_offset; // file offset(lazy load)

  struct hash_elem hash_elem;
};

unsigned spt_hash(const struct hash_elem *p_hash_elem, void *aux UNUSED);
bool spt_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
void spt_free(struct hash_elem *e, void *aux UNUSED);
struct page *spt_lookup(void *vaddr, struct hash *spt);

#endif //PINTOS_47_PAGE_H
