#include <stdbool.h>
#include <hash.h>

#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

bool compare_frames (const struct hash_elem *elem_a, 
            const struct hash_elem *elem_b, void *aux UNUSED);
unsigned hash_frame (const struct hash_elem *elem, void *aux UNUSED);

struct frame {
  struct hash_elem elem;
  void *page;
};

struct hash frame_table;

struct lock frame_table_lock;

bool
compare_frames (const struct hash_elem *elem_a, const struct hash_elem *elem_b,
            void *aux UNUSED)
{
  const struct frame *frame_a = hash_entry (elem_a, struct frame, elem);
  const struct frame *frame_b = hash_entry (elem_b, struct frame, elem);
  return frame_a->page < frame_b->page;
}

unsigned
hash_frame (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct frame *f = hash_entry (elem, struct frame, elem);
  return hash_bytes (&f->page, sizeof f->page);
}

void 
init_frame_table (void) 
{
  lock_init(&frame_table_lock);
  hash_init(&frame_table, hash_frame, compare_frames, NULL);
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

void *
allocate_frame (void) 
{
  void *page = palloc_get_page(PAL_USER | PAL_ZERO);
  if (page == NULL)
  {
    PANIC ("No frames remaining");
  }

  struct frame *frame = malloc (sizeof (struct frame));
  if (frame == NULL)
  {
    PANIC ("Allocation of frame failed (malloc)");
  }

  frame->page = page;
  hash_insert(&frame_table, &frame->elem);
  return page;
}

void
free_frame (void *frame_addr)
{
  struct frame temp_frame;
  temp_frame.page = frame_addr;
  struct hash_elem *e = hash_find (&frame_table, &temp_frame.elem);

  if (e == NULL)
  {
    PANIC ("Frame freeing failed");
  }

  struct frame *f = hash_entry (e, struct frame, elem);
  hash_delete (&frame_table, &f->elem);
  palloc_free_page (frame_addr);
}