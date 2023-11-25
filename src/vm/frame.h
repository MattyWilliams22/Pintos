#ifndef FRAME_H
#define FRAME_H

void init_frame_table(void);
void *allocate_frame (void);
void free_frame (void *frame_addr);

#endif