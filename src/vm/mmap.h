#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include <stdbool.h>
#include "filesys/file.h"
#include "userprog/process.h"

/* Type used for uniquely identifying mapped_files. */
typedef int mapid_t;

/* A mapped file. */
struct mapped_file
{
    mapid_t mapid;          /* Unique id of mapped file. */
    struct file *file;      /* File that is being mapped. */
    void *addr;             /* Address of file in user space. */
    size_t page_count;      /* Number of pages in the file. */
    struct list_elem elem;  /* Used for adding to thread->mapped_files list. */
};

mapid_t mmap (int fd, void *addr);
void munmap (mapid_t id);

#endif /* vm/mmap.h */
