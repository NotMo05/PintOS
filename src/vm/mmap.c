#include "vm/mmap.h"

// mmap table hash function
unsigned mmap_hash(const struct hash_elem *p_hash_elem, void *aux UNUSED) {
    struct mmap_entry* p_mmap_entry = hash_entry(p_hash_elem, struct mmap_entry, hash_elem);
    return hash_int((int) p_mmap_entry->mapid);
}

// mmapp table 'less' comparison function
bool mmap_less(const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED) {
  const struct mmap_entry *a = hash_entry(a_, struct mmap_entry, hash_elem);
  const struct mmap_entry *b = hash_entry(b_, struct mmap_entry, hash_elem);
  return a->mapid < b->mapid;
}

