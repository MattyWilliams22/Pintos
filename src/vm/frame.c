#include <stdbool.h>
#include <hash.h>

#include "vm/frame.h"
#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

bool compare_frames (const struct hash_elem *elem_a, 
            const struct hash_elem *elem_b, void *aux UNUSED);
unsigned hash_frame (const struct hash_elem *elem, void *aux UNUSED);

struct list frame_table;

struct lock frame_table_lock;

void 
init_frame_table (void) 
{
  lock_init(&frame_table_lock);
  list_init(&frame_table);
}

void
acquire_frame_table_lock (void)
{
  lock_acquire(&frame_table_lock);
}

void
release_frame_table_lock (void)
{
  lock_release(&frame_table_lock);
}

struct frame *
allocate_frame (bool pinned) 
{
  void *page_phys_addr = palloc_get_page(PAL_USER);
  if (page_phys_addr == NULL)
  {
    return find_frame_to_evict();
  }

  struct frame *frame = malloc (sizeof (struct frame));
  if (frame == NULL)
  {
    return NULL;
  }
  frame->page_user_addr = NULL;
  frame->page_phys_addr = page_phys_addr;
  frame->pinned = pinned;
  frame->pt = NULL;

  return frame;
}

void
free_frame (struct frame *frame)
{
  list_remove (&frame->elem);
  palloc_free_page (frame->page_phys_addr);
  free (frame);
}

void
pin_frame (struct frame *frame)
{
  if (!frame->pinned)
    frame->pinned = true;
}

void
unpin_frame (struct frame *frame)
{
  if (frame->pinned)
    frame->pinned = false;
}

struct frame *
find_frame_to_evict (void)
{
  size_t size = list_size (&frame_table);
  ASSERT (size > 0);

  /* LRU */
  struct list_elem *e = list_begin (&frame_table);

  while (true)
  {
    if (e == list_end (&frame_table))
      e = list_begin (&frame_table);

    struct frame *frame = list_entry (e, struct frame, elem);

    if (!frame->pinned)
    {
      if (!pagedir_is_accessed (frame->pt->pd, frame->page_user_addr))
        return frame;

      pagedir_set_accessed (frame->pt->pd, frame->page_user_addr, false);
    }

    e = list_next (e);
  }
}

bool
curr_has_ft_lock (void)
{
  return lock_held_by_current_thread (&frame_table_lock);
}