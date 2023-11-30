#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/page.h"
#include "mmap.h"
#include <stdio.h>
#include "threads/thread.h"
#include "threads/vaddr.h"

#define MAP_FAILED ((mapid_t) -1)

mapid_t mmap (int fd, void *addr)  {
    // Check if fd is valid
    if (fd == 0|| fd == 1) {      // *Need to change 0 and 1 to input and output*
        return MAP_FAILED; 
    }

    // Get file
    struct file *file;
    file = process_get_file(fd);

    
    if (file == NULL)
    {
        return MAP_FAILED;
    }

    off_t file_size = file_length (file);

    if (file_size == 0 || (int) addr == 0 || (int) addr % PGSIZE != 0)
    {
        return MAP_FAILED;
    }

    

        
}
