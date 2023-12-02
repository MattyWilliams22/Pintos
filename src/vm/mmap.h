#ifndef MMAP_H
#define MMAP_H

#include "filesys/file.h"
  
typedef int mapid_t;

mapid_t mmap(int fd, void *addr);
void munmap(mapid_t mapid);

struct mapped_file
{
    mapid_t mapid;
    struct file *file;
    void *addr;
    size_t page_count;
    struct list_elem elem;
}

#endif /* MMAP_H */
