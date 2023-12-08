#include <string.h>
#include <debug.h>
#include <stdio.h>

#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "devices/swap.h"
#include "userprog/process.h"

static bool compare_pages (const struct hash_elem *elem_a, 
                           const struct hash_elem *elem_b, void *aux);
static unsigned hash_page (const struct hash_elem *elem, void *aux);
static void remove_page (struct hash_elem *elem, void *aux);
static struct page *search_pt (struct page_table *pt, void *uaddr);
static struct page *make_page (struct page_table *page_table, 
                              void *uaddr, bool init);
static void swap_page_in (struct page *p, struct frame *frame);
static void swap_page_out (struct page *p, bool dirty);
static void destroy_page (struct page *p, bool dirty);

/* Compare the hash uaddr of two pages. */
static bool
compare_pages (const struct hash_elem *elem_a, const struct hash_elem *elem_b,
            void *aux UNUSED)
{
  const struct page *page_a = hash_entry (elem_a, struct page, elem);
  const struct page *page_b = hash_entry (elem_b, struct page, elem);
  return page_a->uaddr < page_b->uaddr;
}

/* Calculates the hash of a page. */
static unsigned
hash_page (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct page *p = hash_entry (elem, struct page, elem);
  return hash_bytes (&p->uaddr, sizeof(p->uaddr));
}

/* Initialises supplemental page table. */
bool
init_pt (struct page_table *page_table)
{
  lock_init (&page_table->lock);

  if ((page_table->pd = pagedir_create ()) == NULL)
  {
    return false;
  }

  if (!hash_init (&page_table->spt, hash_page, compare_pages, page_table->pd))
  {
    return false;
  }
  return true;
}

/* Removes a page from the supplemental page table and frees the page. */
static void
remove_page (struct hash_elem *elem, void *aux UNUSED) 
{
  struct page *page = hash_entry(elem, struct page, elem);
  uint32_t *pd = (uint32_t *) aux;

  bool present_status = page->present;
  bool dirty = false;
  if (present_status)
    dirty = pagedir_is_dirty (pd, page->uaddr);
  destroy_page (page, dirty);
  if (present_status)
    free_frame (page->frame);
  free (page);
}

/* Frees the supplemental page table and the pages stored within it. */
void
free_pt (struct page_table *page_table)
{
  /* Always acquire locks in this order to avoid deadlock. */
  acquire_frame_table_lock ();
  lock_acquire (&page_table->lock);

  uint32_t *page_dir = page_table->pd;
  if (page_dir != NULL)
  {
    page_table->pd = NULL;
    pagedir_activate (NULL);
  }

  /* Destroy the hash table. */
  hash_destroy(&page_table->spt, remove_page);

  /* Ensure the page directory is wiped. */
  if (page_dir != NULL)
  {
    pagedir_destroy (page_dir);
  }

  release_frame_table_lock ();
  lock_destroy (&page_table->lock);
}

/* Find the page with the given user address in the page table. */
static struct page *
search_pt (struct page_table *pt, void *uaddr)
{
  struct hash_elem *e = hash_find (&pt->spt, &(struct page){ .uaddr = uaddr }.elem);
  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

/* Makes a new page at the given user address, 
   freeing any page currently at that address. */
static struct page *
make_page (struct page_table *page_table, void *uaddr, bool init)
{
  /* Search for a page at the given user address in the page table. */
  struct page *page = search_pt (page_table, uaddr);

  /* Check if a page was found with the given user address. */
  if (page == NULL)
  {
    if (!init)
      return NULL;
    
    /* Initialise a new page. */
    struct page *new_page = malloc (sizeof (struct page));
    if (new_page == NULL)
      return NULL;
    
    /* Add the new page to the supplemental page table. */
    new_page->uaddr = uaddr;
    hash_insert (&page_table->spt, &new_page->elem);
    return new_page;
  }

  if (page->present && !curr_has_ft_lock())
  {
    lock_release (&page_table->lock);
    /* Must acquire locks in the same order to avoid deadlock. */
    acquire_frame_table_lock ();
    lock_acquire (&page_table->lock);

    struct page *new_page = make_page (page_table, uaddr, init);

    release_frame_table_lock ();
    return new_page;
  }

  /* Remove the page from the frame table and the page directory. */
  bool present_status = page->present;
  bool dirty = false;
  if (present_status)
  {
    pagedir_clear_page (page_table->pd, uaddr);
    dirty = pagedir_is_dirty (page_table->pd, uaddr);
  }
  destroy_page (page, dirty);
  if (present_status)
    free_frame (page->frame);
  return page;
}

/* Creates a new file page and adds it to the page table. */
bool
create_file_page (struct page_table *page_table, void *uaddr, struct file *id, off_t offset,
                   uint32_t length, bool writable, bool write_back)
{
  lock_acquire (&page_table->lock);

  struct page *page = make_page (page_table, uaddr, true);
  if (page == NULL)
  {
    lock_release (&page_table->lock);
    return false;
  }

  page->uaddr = uaddr;
  page->file = id;
  page->offset = offset;
  page->writable = writable;
  page->type = FILE;
  page->present = false;
  page->write_back = write_back;
  page->length = length;

  lock_release (&page_table->lock);

  return true;
}

/* Creates a new zero page and adds it to the page table. */
bool
create_zero_page (struct page_table *page_table, void *uaddr, bool writable)
{
  lock_acquire (&page_table->lock);

  struct page *page = make_page(page_table, uaddr, true);
  if (page == NULL)
  {
    lock_release (&page_table->lock);
    return false;
  }

  page->uaddr = uaddr;
  page->present = false;
  page->type = ZERO;
  page->writable = writable;

  lock_release (&page_table->lock);

  return true;
}

/* Deletes the page at the given user address from the page table. */
bool
delete_page (struct page_table *page_table, void *uaddr)
{
  lock_acquire (&page_table->lock);

  struct page *page = make_page (page_table, uaddr, false);
  if (page == NULL)
  {
    lock_release (&page_table->lock);
    return false;
  }

  hash_delete (&page_table->spt, &page->elem);
  free (page);

  lock_release (&page_table->lock);

  return true;
}

/* Returns true if the page at the given user address is free to use. */
bool
available_page (struct page_table *page_table, void *uaddr) {
  return is_user_vaddr (uaddr) && !already_mapped (page_table, uaddr)
    && !in_stack (uaddr);
}

/* Activates the page table. */
void
activate_pt (struct page_table *page_table)
{
  pagedir_activate (page_table->pd);
}

/* Returns true if there is already a page with address uaddr. */
bool
already_mapped (struct page_table *page_table, void *uaddr)
{
  lock_acquire (&page_table->lock);
  struct page *page = search_pt (page_table, uaddr);
  lock_release (&page_table->lock);
  return page != NULL;
}

/* Returns true if uaddr is within the stack. */
bool
in_stack (void *uaddr)
{
  return (uaddr < PHYS_BASE) && (PHYS_BASE - STACK_LIMIT <= uaddr);
}

/* Makes the page at uaddr in the page table present so that it can be accessed. */
bool
load_page (struct page_table *pt, void *uaddr)
{
  acquire_frame_table_lock ();
  lock_acquire (&pt->lock);

  /* Ensure page is in the page table, if not return false. */
  struct page *page = search_pt (pt, uaddr);
  if (page == NULL)
  {
    release_frame_table_lock ();
    lock_release (&pt->lock);
    return false;
  }

  /* If the page is already present, return true. */
  if (page->present)
  {
    release_frame_table_lock ();
    lock_release (&pt->lock);
    return true;
  }

  /* Allocate a new frame for this page. */
  struct frame *frame = allocate_frame (false);
  if (frame == NULL)
  {
    release_frame_table_lock ();
    lock_release (&pt->lock);
    return false;
  }

  /* Initialise values for the frame, evicting the previous frame if necessary. */
  if (frame->pt != NULL)
  {
    struct page_table *old_pt = frame->pt;
    void *old_user_page = frame->page_user_addr;

    if (old_pt != pt)
      lock_acquire (&old_pt->lock);

    struct page *evicted_page = search_pt (old_pt, old_user_page);

    frame->pt = pt;
    frame->page_user_addr = uaddr;

    release_frame_table_lock ();

    pagedir_clear_page (old_pt->pd, old_user_page);
    bool dirty = pagedir_is_dirty (old_pt->pd, old_user_page);

    swap_page_out (evicted_page, dirty);

    if (old_pt != pt)
      lock_release (&old_pt->lock);
  }
  else
  {
    frame->pt = pt;
    frame->page_user_addr = uaddr;

    release_frame_table_lock ();
  }

  /* Swap the page into the frame and set dirty and accessed bits to false. */
  swap_page_in (page, frame);
  pagedir_set_page (pt->pd, uaddr, frame->page_phys_addr, page->writable);
  pagedir_set_dirty (pt->pd, uaddr, false);
  pagedir_set_accessed (pt->pd, uaddr, false);

  lock_release (&pt->lock);
  return true;
}

/* Load data from the page's address and set present to true. */
static void
swap_page_in (struct page *p, struct frame *frame)
{
  p->present = true;
  p->frame = frame;

  switch (p->type)
  {
    case ZERO:
      memset (p->frame->page_phys_addr, 0, PGSIZE);
      break;

    case FILE:
      acquire_filesystem_lock ();
      off_t n = file_read_at (p->file, p->frame->page_phys_addr, p->length, p->offset);
      release_filesystem_lock ();
      memset (p->frame->page_phys_addr + n, 0, PGSIZE - n);
      break;

    case SWAP:
      swap_in (p->frame->page_phys_addr, p->slot);
      break;
  }
}

/* Save page's data and set present to false. */
static void
swap_page_out (struct page *p, bool dirty)
{
  p->present = false;

  switch (p->type)
  {
    case ZERO:
      if (dirty)
      {
        p->type = SWAP;
        p->slot = swap_out (p->frame->page_phys_addr);
      }
      break;

    case SWAP:
      p->type = SWAP;
      p->slot = swap_out (p->frame->page_phys_addr);
      break;

    case FILE:
      if (dirty && p->write_back)
      {
        acquire_filesystem_lock ();
        file_write_at (p->file, p->frame->page_phys_addr, p->length, p->offset);
        release_filesystem_lock ();
      }
      else if (dirty)
      {
        p->type = SWAP;
        p->slot = swap_out (p->frame->page_phys_addr);
      }
      break;
  }
}

/* Frees all data saved by a page. */
static void
destroy_page (struct page *p, bool dirty)
{
  switch (p->type)
  {
    case ZERO:
      break;

    case SWAP:
      if (!p->present)
        swap_drop (p->slot);
      break;

    case FILE:
      if (p->present && dirty && p->write_back)
      {
        acquire_filesystem_lock ();
        file_write_at (p->file, p->frame->page_phys_addr, p->length, p->offset);
        release_filesystem_lock ();
      }
      break;
  }
}
