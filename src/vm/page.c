#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"

/* Supplemental page table. (per process) */
struct page
{
  struct hash_elem elem;
  struct frame *frame;
  void *key;
}

/* Compare hash key of two pages. */
bool
compare_pages (const struct hash_elem *elem_a, const struct hash_elem *elem_b,
            void *aux UNUSED)
{
  const struct page *page_a = hash_entry (elem_a, struct page, elem);
  const struct page *page_b = hash_entry (elem_b, struct page, elem);
  return page_a->key < page_b->key;
}

/* Calculates hash from hash key. */
unsigned
hash_page (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct page *p = hash_entry (elem, struct page, elem);
  return hash_bytes (&p->key, sizeof(p->key));
}

/* Initialises supplemental page table and its fields. 
   Returns true if initialised successfully. */
bool
init_spt (struct spt *spt)
{
  lock_init(&spt->lock);

  spt->page_dir = pagedir_create();
  if (spt->page_dir == NULL) {
    return false;
  }

  if (!hash_init(&spt->page_table, hash_page, compare_pages, spt->page_dir)) {
    return false;
  }

  return true;
}

/* Finds the page in the supplemental page table with the given hash key. */
struct page *
get_page (struct spt *spt, void *key)
{
  struct page temp_page;
  temp_page.key = key;
  struct hash_elem *elem = hash_find(&spt->page_table, temp_page.elem);
    
  if (elem == NULL) {
    return NULL;
  }
  return hash_entry(elem, struct page, elem);
}

/* Removes page with given hash key from page table.
   Returns true if a page was removed successfully. */
bool
remove_page (struct spt *spt, void *key)
{
  // need to set page
  struct page *page = get_page(spt, key);
  if (page == NULL) {
    lock_release(&spt->lock);
    return false;
  } 

  hash_delete(&spt->page_table, &page->elem);
  free(page);

  lock_release(&spt->lock);
  return true;
}

/* Frees the page given by the hash elem p. 
   Aux allows use as a hash_action_func. */
void
free_page (struct page *page, void *aux)
{
  uint32_t *page_dir = (uint32_t *)aux;
  free_frame(page->frame);
  free(page); 
}

/* Frees the supplemental page table. */
void
free_spt (struct spt *spt)
{
  /* Order of locks is crucial to avoid deadlock. */
  acquire_frame_table_lock();
  lock_acquire(*spt->lock);

  uint32_t pd = spt->page_dir;
  if (pd != NULL) {
    spt->page_dir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }

  hash_destroy(&spt->page_table, free_page);

  release_frame_table_lock();
  lock_destroy(&spt->lock);
}