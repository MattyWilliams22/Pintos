#include "mmap.h"
#include <stdio.h>
#include <hash.h>
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/frame.h"
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
    struct hash *page_table = current->page_table;
    for (size_t i = 0; i < page_count; i++) {
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
    for (size_t i = 0; i < page_count; i++) {
        size_t bytes_to_read = PGSIZE >= bytes_left ? PGSIZE : bytes_left;

        /* Unsure what read_bytes and zero_bytes are in create_file_page function. */
        create_file_page(page_table, addr, new_mapped_file->file, offset,
                   bytes_to_read, PGSIZE - bytes_to_read, true, true);
        
        addr += PGSIZE;
        offset += PGSIZE;
        bytes_left -= bytes_to_read;
    }

    return new_mapped_file->mapid;
}

void munmap(mapid_t id) {
    struct thread *current = thread_current();
    struct list *mapped_files = &current->mapped_files;

    struct list_elem *e;
    struct mapped_file *target_mapped_file = NULL;

    for (e = list_begin(mapped_files); e != list_end(mapped_files); e = list_next(e)) {
        struct mapped_file *mf = list_entry(e, struct mapped_file, elem);
        if (mf->mapid == id) {
            target_mapped_file = mf;
            break;
        }
    }

    /* No mapping found. */
    if (target_mapped_file == NULL) {
        return; 
    }

    /* Unmap each virtual page in the range. */ 
    void *addr = target_mapped_file->addr;
    size_t page_count = target_mapped_file->page_count;
 
    for (size_t i = 0; i < page_count; i++) {
        struct page *page = search_pt(current->page_table, addr);
        if (page != NULL) {
            if (page->type == FRAME) {
                if (page->writable && page->kernel_addr != NULL) {
                    /* Write back modified page to file if necessary. */ 
                    if (page->writable) {
                        switch (page->type) {
                            case FILE:
                                acquire_filesystem_lock();
                                struct file *file = file_reopen(page->file);
                                release_filesystem_lock();
                                if (file != NULL) {
                                    file_write_at(file, page->kernel_addr, page->read_bytes, page->offset);
                                    file_close(file);
                                }
                                break;
                            case ZERO:
                                /* No need to write back for zero pages. */ 
                                break;
                            default:
                                PANIC("Unexpected page type");
                        }
                    }
                }
                /* Free the frame associated with the page. */ 
                free_frame(page->kernel_addr);
            }
            /* Mark the page as not present in the page table. */ 
            remove_pt(current->page_table, addr);
        }
        addr += PGSIZE;
    }

    /* Close the actual file associated with the mapped file. */
    acquire_filesystem_lock();
    file_close(target_mapped_file->file);
    release_filesystem_lock();

    /* Remove the mapping from the process's list of mapped files. */ 
    list_remove(&target_mapped_file->elem);

    /* Free the resources associated with the mapped file structure. */ 
    free(target_mapped_file);
}

