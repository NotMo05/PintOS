#include "page.h"
#include <stdio.h>
#include "threads/malloc.h"
#include "devices/serial.h"

// SPT hash function
unsigned spt_hash(const struct hash_elem *p_hash_elem, void *aux UNUSED) {
  struct page* page = hash_entry(p_hash_elem, struct page, hash_elem);
  return hash_int((int) page->vaddr);
}

// SPT 'less' comparison function
bool spt_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  const struct page* a = hash_entry(a_, struct page, hash_elem);
  const struct page* b = hash_entry(b_, struct page, hash_elem);
  return a->vaddr < b->vaddr;
}

// SPT free function for hash_destroy
void spt_free(struct hash_elem *e, void *aux UNUSED) {
  struct page* page = hash_entry(e, struct page, hash_elem);
  free(page);
}

// SPT lookup by vaddr function
struct page *spt_lookup(void *vaddr, struct hash *spt) {
  lock_acquire(&spt_lock);
  struct page page;
  page.vaddr = vaddr;
  struct hash_elem *e = hash_find(spt, &page.hash_elem);
  lock_release(&spt_lock);
  return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}