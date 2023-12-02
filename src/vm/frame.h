#ifndef FRAME_H
#define FRAME_H

#include "threads/palloc.h"
#include "threads/thread.h"

void init_frame_table(void);
void *allocate_frame (enum palloc_flags flags, void *user_addr);
void free_frame (void *frame_addr);

void acquire_frame_table_lock (void);
void release_frame_table_lock (void);
void frame_reclaim (struct thread *owner);
#endif