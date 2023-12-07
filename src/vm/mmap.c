#include "mmap.h"
#include <stdio.h>
#include <list.h>
#include <stdbool.h>
#include "userprog/syscall.h"
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

#define MAP_FAILED ((mapid_t) -1)

static bool
compare_mapid (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct mapped_file *file_a = list_entry (a, struct mapped_file, elem);
  struct mapped_file *file_b = list_entry (b, struct mapped_file, elem);
  return file_a->mapid < file_b->mapid;
}

mapid_t mmap (int fd, void *addr)  {

  /* Check validity of inputs */
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || addr == NULL || pg_ofs(addr) != 0) {
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
  if (file_size == 0)
  {
    return MAP_FAILED;
  }

  size_t page_count = file_size / PGSIZE;
  if (file_size % PGSIZE != 0) {
    page_count++;
  }
  
  struct thread *current = thread_current();
  struct page_table *page_table = &current->page_table;
  for (size_t i = 0; i < page_count; i++) {
    if (!available_page(page_table, addr + PGSIZE * i)) {
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
  struct list_elem *max = list_max (&current->mapped_files, compare_mapid, NULL);
  mapid_t mapid = (max == list_end (&current->mapped_files))
                   ? 1
                   : (list_entry (max, struct mapped_file, elem)->mapid + 1);

  list_push_back(&current->mapped_files, &new_mapped_file->elem);
  new_mapped_file->mapid = mapid;
  new_mapped_file->file = file;
  new_mapped_file->addr = addr;
  new_mapped_file->page_count = page_count;

  /* Create pages for all of the data in the file. */
  size_t bytes_left = file_size;
  off_t offset = 0;
  for (size_t i = 0; i < page_count; i++) {
    size_t bytes_to_read = bytes_left < PGSIZE ? bytes_left : PGSIZE;

    create_file_page(page_table, addr, new_mapped_file->file, offset,
           bytes_to_read, true, true);
    
    addr += PGSIZE;
    offset += PGSIZE;
    bytes_left -= bytes_to_read;
  }

  return mapid;
}

void munmap(mapid_t id) {
  struct thread *current = thread_current();
  struct list *mapped_files = &current->mapped_files;
  struct list_elem *e;
  struct mapped_file *target_mapped_file = NULL;

  /* Find the mapped file with the same mapid in the list of mapped files. */
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

  /* Delete every page in the file. */
  for (size_t i = 0; i < target_mapped_file->page_count; i++)
    delete_page (&current->page_table, target_mapped_file->addr + i * PGSIZE);

  /* Close the actual file associated with the mapped file. */
  acquire_filesystem_lock();
  file_close(target_mapped_file->file);
  release_filesystem_lock();

  /* Remove the mapping from the process's list of mapped files. */ 
  list_remove(&target_mapped_file->elem);

  /* Free the resources associated with the mapped file structure. */ 
  free(target_mapped_file);
}

