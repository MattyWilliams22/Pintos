#ifndef FRAME_H
#define FRAME_H

#include "threads/palloc.h"

/* Information about a single frame. */
struct frame {
  struct list_elem elem;        /* Element used to add frame to frame table. */
  void *page_phys_addr;         /* Physical address of page in memory. */
  void *page_user_addr;         /* User address of page in memory. */
  bool pinned;                  /* Used to show frame must not be evicted. */
  struct page_table *pt;        /* The page table containing the page. */
};

void init_frame_table (void);
struct frame *allocate_frame (bool pinned);
void free_frame (struct frame *frame);

void acquire_frame_table_lock (void);
void release_frame_table_lock (void);

struct frame *find_frame_to_evict (void);

bool curr_has_ft_lock (void);

#endif
