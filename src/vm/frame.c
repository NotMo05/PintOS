#include <debug.h>
#include <stdio.h>
#include "frame.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "devices/swap.h"
#include "page.h"
#include "filesys/file.h"

// Frame table hash function
unsigned frame_hash(const struct hash_elem *p_hash_elem, void *aux UNUSED) {
  struct frame_entry* p_frame_entry = hash_entry(p_hash_elem, struct frame_entry, hash_elem);
  return hash_int((int) p_frame_entry->frame_addr);
}

// Frame table 'less' comparison function
bool frame_less(const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED) {
  const struct frame_entry *a = hash_entry(a_, struct frame_entry, hash_elem);
  const struct frame_entry *b = hash_entry(b_, struct frame_entry, hash_elem);
  return a->frame_addr < b->frame_addr; 
}

// Frame table eviction using second-change algorithm
// Should be called with frame_table_lock acquired
struct frame_entry *frame_evict(void *upage) {
  struct hash_iterator i;
  hash_first(&i, &frame_hash_table);
  while (true) {
    if (!hash_next(&i)) {
      hash_first(&i, &frame_hash_table);
      hash_next(&i);
    }
    struct frame_entry *frame = hash_entry(hash_cur(&i), struct frame_entry, hash_elem);
    void *faddr = frame->upage_addr;
    struct page *page = spt_lookup(faddr, &frame->owner->spt);

    if (!pagedir_is_accessed(frame->owner->pagedir, faddr)) {
      page->swap_slot = swap_out(faddr);
      page->swapped = true;
      page->frame = NULL;

      frame->owner = thread_current();
      pagedir_clear_page(frame->owner->pagedir, frame->upage_addr);
      frame->upage_addr = upage;


      return frame;
    } else {
      pagedir_set_accessed(frame->owner->pagedir, frame->upage_addr, false);
    }
  }

  // Should never reach here
  printf("frame evict returned NULL, can't happen\n");
  return NULL;
}

// palloc_get_page() wrapper function to add frame table entry
struct frame_entry *frame_alloc(enum palloc_flags flags, void *upage) {
  ASSERT(flags & PAL_USER);
  lock_acquire(&frame_table_lock);
  void *frame = palloc_get_page(flags);
  if (frame == NULL) { // No free frame so We evict one.
    struct frame_entry *evicted = frame_evict(upage);
    if (evicted == NULL) {
      PANIC("No frame to evict!");
    }
    lock_release(&frame_table_lock);
    return evicted;
  }
  struct frame_entry *f_entry = malloc(sizeof *f_entry);
  if (f_entry == NULL) { // Unable to allocate memory for frame table entry
      palloc_free_page(frame);
      return NULL;
  }
  f_entry->frame_addr = frame; 
  f_entry->upage_addr = upage;
  f_entry->owner = thread_current();
  hash_insert(&frame_hash_table, &f_entry->hash_elem);
  lock_release(&frame_table_lock);
  return f_entry;
}

// palloc_free_page() wrapper function to remove frame table entry
void frame_free(void *frame) {
  lock_acquire(&frame_table_lock);
  struct frame_entry temp_f_entry;
  temp_f_entry.frame_addr = frame;

  struct hash_elem *h_e = hash_find(&frame_hash_table, &temp_f_entry.hash_elem);
  // We should always find a hash table entry for the given frame (I think)
  ASSERT(h_e != NULL);
  if (h_e != NULL) {
    struct frame_entry *f_entry = hash_entry(h_e, struct frame_entry, hash_elem);
    hash_delete(&frame_hash_table, h_e);
    free(f_entry);
  }
  lock_release(&frame_table_lock);
  palloc_free_page(frame);
}