#include <stdbool.h>
#include <hash.h>

#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

bool compare_frames (const struct hash_elem *elem_a, 
            const struct hash_elem *elem_b, void *aux UNUSED);
unsigned hash_frame (const struct hash_elem *elem, void *aux UNUSED);

struct frame {
  struct hash_elem elem;
  void *page_phys_addr;
  void *page_user_addr;
  struct thread* owner;
};

struct hash frame_table;

struct lock frame_table_lock;

bool
compare_frames (const struct hash_elem *elem_a, const struct hash_elem *elem_b,
            void *aux UNUSED)
{
  const struct frame *frame_a = hash_entry (elem_a, struct frame, elem);
  const struct frame *frame_b = hash_entry (elem_b, struct frame, elem);
  return frame_a->page_phys_addr < frame_b->page_phys_addr;
}

unsigned
hash_frame (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct frame *f = hash_entry (elem, struct frame, elem);
  return hash_bytes (&f->page_phys_addr, sizeof f->page_phys_addr);
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
allocate_frame (enum palloc_flags flags, void *user_addr) 
{
  acquire_frame_table_lock();
  void *page_phys_addr = palloc_get_page(flags);
  if (page_phys_addr == NULL)
  {
    PANIC ("No frames remaining");
  }

  struct frame *frame = malloc (sizeof (struct frame));
  if (frame == NULL)
  {
    PANIC ("Allocation of frame failed (malloc)");
  }
  frame->owner = thread_current ();
  frame->page_user_addr = user_addr;
  frame->page_phys_addr = page_phys_addr;
  hash_insert(&frame_table, &frame->elem);
  release_frame_table_lock();
  return page_phys_addr;
}

void
free_frame (void *frame_addr)
{
  struct frame temp_frame;
  temp_frame.page_phys_addr = frame_addr;
  acquire_frame_table_lock();
  struct hash_elem *e = hash_find (&frame_table, &temp_frame.elem);

  if (e == NULL)
  {
    PANIC ("Frame freeing failed");
  }

  struct frame *f = hash_entry (e, struct frame, elem);
  hash_delete (&frame_table, &f->elem);
  if (f != NULL)
  {
    free (f);
  }
  palloc_free_page (frame_addr);
  release_frame_table_lock();
}

void
frame_reclaim(struct thread *owner)
{
  acquire_frame_table_lock();

  struct hash_iterator iter;
  hash_first(&iter, &frame_table);

  while (hash_next(&iter))
  {
    struct frame *f = hash_entry(hash_cur(&iter), struct frame, elem);

    // Check if the frame == owner 
    if (f->owner == owner)
    {
      void *frame_addr = f->page_phys_addr;

      // Remove and free the frame from the hash table
      free_frame(frame_addr);
    }
  }

  release_frame_table_lock();
}
