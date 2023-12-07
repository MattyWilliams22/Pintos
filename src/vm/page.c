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
static struct page *search_pt (struct page_table *pt, void *user_addr);
static struct page *get_page (struct page_table *page_table, 
                              void *key, bool create);
static void swap_page_in (struct page *p, struct frame *frame);
static void swap_page_out (struct page *p, bool dirty);
static void page_destroy (struct page *p, bool dirty);

/* Compare hash key of two pages. */
static bool
compare_pages (const struct hash_elem *elem_a, const struct hash_elem *elem_b,
            void *aux UNUSED)
{
  const struct page *page_a = hash_entry (elem_a, struct page, elem);
  const struct page *page_b = hash_entry (elem_b, struct page, elem);
  return page_a->key < page_b->key;
}

/* Calculates hash from hash key. */
static unsigned
hash_page (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct page *p = hash_entry (elem, struct page, elem);
  return hash_bytes (&p->key, sizeof(p->key));
}

/* Initializes supplemental page table. */
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

static void
remove_page (struct hash_elem *elem, void *aux UNUSED) 
{
  struct page *page = hash_entry(elem, struct page, elem);
  uint32_t *pd = (uint32_t *) aux;

  bool present_status = page->present;
  bool dirty = false;
  if (present_status)
    dirty = pagedir_is_dirty (pd, page->key);
  page_destroy (page, dirty);
  if (present_status)
    free_frame (page->frame);
  free (page);
}

void
free_pt (struct page_table *page_table)
{
  /* Always acquire locks in this order to avaoid deadlock. */
  acquire_frame_table_lock ();
  lock_acquire (&page_table->lock);

  uint32_t *page_dir = page_table->pd;
  if (page_dir != NULL)
  {
    page_table->pd = NULL;
    pagedir_activate (NULL);
  }

  hash_destroy(&page_table->spt, remove_page);

  if (page_dir != NULL)
  {
    pagedir_destroy (page_dir);
  }

  release_frame_table_lock ();
  lock_destroy (&page_table->lock);
}

static struct page *
search_pt (struct page_table *pt, void *user_addr)
{
  struct hash_elem *e = hash_find (&pt->spt, &(struct page){ .key = user_addr }.elem);
  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

static struct page *
get_page (struct page_table *page_table, void *key, bool create)
{
  bool ft_locked = curr_has_ft_lock ();

  struct page *page = search_pt (page_table, key);

  if (page == NULL)
  {
    if (!create)
      return NULL;

    struct page *res = malloc (sizeof (struct page));
    if (res == NULL)
      return NULL;

    res->key = key;
    hash_insert (&page_table->spt, &res->elem);
    return res;
  }

  if (page->present && !ft_locked)
  {
    lock_release (&page_table->lock);
    acquire_frame_table_lock ();
    lock_acquire (&page_table->lock);

    struct page *res = get_page (page_table, key, create);

    release_frame_table_lock ();
    return res;
  }

  bool present_status = page->present;
  bool dirty = false;
  if (present_status)
  {
    pagedir_clear_page (page_table->pd, key);
    dirty = pagedir_is_dirty (page_table->pd, key);
  }
  page_destroy (page, dirty);
  if (present_status)
    free_frame (page->frame);
  return page;
}

bool
create_file_page (struct page_table *page_table, void *user_page, struct file *id, off_t offset,
                   uint32_t length, bool writable, bool write_back)
{
  lock_acquire (&page_table->lock);

  struct page *page = get_page (page_table, user_page, true);
  if (page == NULL)
  {
    lock_release (&page_table->lock);
    return false;
  }

  page->key = user_page;
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

bool
create_zero_page (struct page_table *page_table, void *user_addr, bool writable)
{
  lock_acquire (&page_table->lock);

  struct page *page = get_page(page_table, user_addr, true);
  if (page == NULL)
  {
    lock_release (&page_table->lock);
    return false;
  }

  page->key = user_addr;
  page->present = false;
  page->type = ZERO;
  page->writable = writable;

  lock_release (&page_table->lock);

  return true;
}

bool
delete_page (struct page_table *page_table, void *user_addr)
{
  lock_acquire (&page_table->lock);

  struct page *page = get_page (page_table, user_addr, false);
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

/* Returns true if the page at address user_page is free to use. */
bool
available_page (struct page_table *page_table, void *user_page) {
  return is_user_vaddr (user_page) && !already_mapped (page_table, user_page)
    && !in_stack (user_page);
}

void
activate_pt (struct page_table *page_table)
{
  pagedir_activate (page_table->pd);
}

bool
already_mapped (struct page_table *page_table, void *user_page)
{
  lock_acquire (&page_table->lock);
  struct page *page = search_pt (page_table, user_page);
  lock_release (&page_table->lock);
  return page != NULL;
}

bool
in_stack (void *user_page)
{
  return (user_page < PHYS_BASE) && (PHYS_BASE - STACK_LIMIT <= user_page);
}

bool
make_present (struct page_table *pt, void *user_page)
{
  acquire_frame_table_lock ();
  lock_acquire (&pt->lock);

  struct page *page = search_pt (pt, user_page);
  if (page == NULL)
  {
    release_frame_table_lock ();
    lock_release (&pt->lock);
    return false;
  }
  if (page->present)
  {
    release_frame_table_lock ();
    lock_release (&pt->lock);
    return true;
  }

  struct frame *frame = allocate_frame (false);
  if (frame == NULL)
  {
    release_frame_table_lock ();
    lock_release (&pt->lock);
    return false;
  }

  if (frame->pt != NULL)
  {
    struct page_table *old_pt = frame->pt;
    void *old_user_page = frame->page_user_addr;

    if (old_pt != pt)
      lock_acquire (&old_pt->lock);

    struct page *eviction_page = search_pt (old_pt, old_user_page);

    frame->pt = pt;
    frame->page_user_addr = user_page;

    release_frame_table_lock ();

    pagedir_clear_page (old_pt->pd, old_user_page);
    bool dirty = pagedir_is_dirty (old_pt->pd, old_user_page);

    swap_page_out (eviction_page, dirty);

    if (old_pt != pt)
      lock_release (&old_pt->lock);
  }
  else
  {
    frame->pt = pt;
    frame->page_user_addr = user_page;

    release_frame_table_lock ();
  }

  swap_page_in (page, frame);

  pagedir_set_page (pt->pd, user_page, frame->page_phys_addr, page->writable);
  pagedir_set_dirty (pt->pd, user_page, false);
  pagedir_set_accessed (pt->pd, user_page, false);

  lock_release (&pt->lock);
  return true;
}

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
      swap_in (p->frame->page_phys_addr, p->swap);
      break;
  }
}

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
        p->swap = swap_out (p->frame->page_phys_addr);
      }
      break;

    case SWAP:
      p->type = SWAP;
      p->swap = swap_out (p->frame->page_phys_addr);
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
        p->swap = swap_out (p->frame->page_phys_addr);
      }
      break;
  }
}

static void
page_destroy (struct page *p, bool dirty)
{
  switch (p->type)
  {
    case ZERO:
      break;

    case SWAP:
      if (!p->present)
        swap_drop (p->swap);
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
