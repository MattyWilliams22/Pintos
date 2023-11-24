#include <hash.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "lib/kernel/debug.h"
#include "lib/kernel/list.h"


struct frame {
  struct hash_elem elem;
  void *page;
}

struct hash frame_table;

bool 
compare_frames (const struct frame_a, const struct frame_b) 
{
  void *page_a = frame_a->page;
  void *page_b = frame_b->page;
  return page_a < page_b;
}

void 
init_frame_table() 
{
  hash_init(&frame_table);
}

void 
allocate_frame () 
{
  struct frame *frame;
  void *page = palloc_get_page(PAL_USER | PAL_ZERO);
  if (page == NULL) {
    PANIC("Failed to get page.");
  }
  frame->page = page;
  hash_insert(&frame_table, &frame->elem);

}

void
free_frame (struct frame* frame)
{
  hash_delete(&frame_table, &frame->elem);
  free(&frame->elem);
  free(frame);
}

void 
remove_page () {

  /* Evict a frame from the frame table. */
  struct hash_iterator i;

  hash_first(&i, &frame_table);
  free_frame(hash_entry(hash_next(&i), struct frame, elem));

}