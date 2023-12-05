#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include <stdbool.h>
#include "filesys/file.h"
#include "userprog/process.h"
  
typedef int mapid_t;

struct mapped_file
{
    mapid_t mapid;
    struct file *file;
    void *addr;
    size_t page_count;
    struct list_elem elem;
};

mapid_t mmap (int fd, void *addr);
void munmap (mapid_t id);

#endif /* vm/mmap.h */
