#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "vm/page.h"
#include "mmap.h"
#include <stdio.h>
#include "threads/thread.h"
#include "threads/vaddr.h"

#define MAP_FAILED ((mapid_t) -1)

mapid_t mmap (int fd, void *addr)  {

    /* Check validity of inputs */
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || addr == NULL) {
        return MAP_FAILED; 
    }

    /* Get file using fd */
    acquire_filesystem_lock();
    struct file *file = process_get_file(fd);
    release_filesystem_lock();
    if (file == NULL) {
        return MAP_FAILED;
    }

    acquire_filesystem_lock();
    file = file_reopen(file);
    release_filesystem_lock();
    if (file == NULL) {
        return MAP_FAILED;
    }

    acquire_filesystem_lock();
    off_t file_size = file_length (file);
    release_filesystem_lock();
    if (file_size == 0 || (int) addr == 0 || (int) addr % PGSIZE != 0)
    {
        return MAP_FAILED;
    }

    size_t page_count = file_size % PGSIZE;
    if (file_size % PGSIZE != 0) {
        page_count++;
    }
    
    struct thread *current = thread_current();
    struct page_table page_table = &current->page_table;
    struct list *list = &current -> mapped_files;
    for (int i = 0; i < page_count; i++) {
        if (!available_page(page_table, i * PGSIZE + addr)) {
            return MAP_FAILED;
        }
    }

    /* Allocate memory to new mapped file. */
    struct mapped_file *new_mapped_file;
    new_mapped_file = (struct mapped_file *) malloc (sizeof(struct mapped_file));
    if (new_mapped_file == NULL) {
        return MAP_FAILED;
    }

    /* Set initial values for the new mapped file. */

        /* Always incrementing next_mapid by 1. 
           May want to check for other unused mapids. */
    mapid_t mapid = (mapid_t) current->next_mapid++;
    list_push_back(&current->mapped_files, &new_mapped_file->elem);
    new_mapped_file->mapid = mapid;
    new_mapped_file->file = file;
    new_mapped_file->addr = addr;
    new_mapped_file->page_count = page_count;

    /* Lazy load file. */
    size_t bytes_left = file_size;
    off_t offset = 0;
    for (int i = 0; i < page_count; i++) {
        size_t bytes_to_read = PGSIZE >= bytes_left ? PGSIZE : bytes_left;

        /* Unsure what read_bytes and zero_bytes are in create_file_page function. */
        create_file_page(page_table, addr, new_mapped_file->file, offset,
                   bytes_to_read, bytes_to_read, true, true);
        
        addr += PGSIZE;
        offset += PGSIZE;
        bytes_left -= bytes_to_read;
    }

    return new_mapped_file->mapid;
}
