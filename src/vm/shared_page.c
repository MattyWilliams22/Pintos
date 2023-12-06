#include <string.h>
#include <debug.h>

#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"

/* Hash function*/
static unsigned
shared_page_hash(const struct hash_elem *e, void *aux UNUSED) {
    const struct shared_page *entry = hash_entry(e, struct shared_page, hash_elem);
    return hash_bytes(&entry->upage, sizeof(entry->upage));
}

/* Comparison function */
static bool
shared_page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
    const struct shared_page *entry_a = hash_entry(a, struct shared_page, hash_elem);
    const struct shared_page *entry_b = hash_entry(b, struct shared_page, hash_elem);
    return entry_a->upage < entry_b->upage;
}

/* Check if page is shared */
static bool is_shared_page(void *upage) {
    struct shared_page entry;
    entry.upage = upage;
    struct hash_elem *e = hash_find(&thread_current()->shared_pages, &entry.hash_elem);
    return e != NULL;
}

/* Share a page among multiple processes */
static void share_page(void *upage, struct file *file, off_t ofs, size_t read_bytes, bool writable) {
    struct thread *t = thread_current();
    
    struct shared_page *entry = malloc(sizeof(struct shared_page));
    if (entry == NULL)
        PANIC("Failed to allocate memory for shared_page");

    entry->upage = upage;
    entry->file = file;
    entry->ofs = ofs;
    entry->read_bytes = read_bytes;

    hash_insert(&t->shared_pages, &entry->hash_elem);
}