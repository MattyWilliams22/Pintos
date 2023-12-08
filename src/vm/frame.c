#include <stdbool.h>
#include <hash.h>

#include "vm/frame.h"
#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

/* The frame table. */
struct list frame_table;

/* Lock used to control access to the frame table. */
struct lock frame_table_lock;

/* Initialises frame table. */
void 
init_frame_table (void) 
{
  lock_init(&frame_table_lock);
  list_init(&frame_table);
}

/* Locks the frame table. */
void
acquire_frame_table_lock (void)
{
  lock_acquire(&frame_table_lock);
}

/* Unlocks the frame table. */
void
release_frame_table_lock (void)
{
  lock_release(&frame_table_lock);
}

/* Allocates a frame to the frame table. */
struct frame *
allocate_frame (bool pinned) 
{
  ASSERT (curr_has_ft_lock());

  void *page_phys_addr = palloc_get_page(PAL_USER);
  if (page_phys_addr == NULL)
  {
    return find_frame_to_evict();
  }

  struct frame *to_add = malloc (sizeof (struct frame));
  if (to_add == NULL)
  {
    return NULL;
  }
  
  list_push_back(&frame_table, &to_add->elem);
  to_add->page_user_addr = NULL;
  to_add->page_phys_addr = page_phys_addr;
  to_add->pinned = pinned;
  to_add->pt = NULL;

  return to_add;
}

/* Frees a frame and removes it from the frame table. */
void
free_frame (struct frame *frame)
{
  ASSERT (curr_has_ft_lock());

  list_remove (&frame->elem);
  palloc_free_page (frame->page_phys_addr);
  free (frame);
}

struct frame *
find_frame_to_evict (void)
{
  ASSERT (curr_has_ft_lock());

  size_t size = list_size (&frame_table);
  ASSERT (size > 0);

  /* Iterate through frame table until a frame is found to evict. */
  struct list_elem *e = list_begin (&frame_table);
  while (true)
  {
    if (e == list_end (&frame_table))
      e = list_begin (&frame_table);

    struct frame *frame = list_entry (e, struct frame, elem);

    /* Pinned frames cannot be evicted from the frame table. */
    if (!frame->pinned)
    {
      if (!pagedir_is_accessed (frame->pt->pd, frame->page_user_addr))
        return frame;

      pagedir_set_accessed (frame->pt->pd, frame->page_user_addr, false);
    }

    e = list_next (e);
  }
}

/* Returns true if the current thread owns the frame table lock. */
bool
curr_has_ft_lock (void)
{
  return lock_held_by_current_thread (&frame_table_lock);
}