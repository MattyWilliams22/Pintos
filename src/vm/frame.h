#ifndef FRAME_H
#define FRAME_H

#include "threads/palloc.h"

struct page_table;

struct frame {
  struct list_elem elem;
  void *page_phys_addr;
  void *page_user_addr;
  bool pinned;
  struct page_table *pt;
};

void init_frame_table(void);
void *allocate_frame (bool pinned);
void free_frame (struct frame *frame);

void acquire_frame_table_lock (void);
void release_frame_table_lock (void);

struct frame *find_frame_to_evict (void);
void pin_frame (struct frame *frame);
void unpin_frame (struct frame *frame);

bool curr_has_ft_lock (void);

#endif