#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"

/* Lock used to restrict access to the file system. */
static struct lock filesystem_lock;

/* Open file to be added to list of open files. */
struct open_file
{
  struct file *file;
  int fd;
  struct list_elem elem;
};

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init(&filesystem_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int system_call = memcpy(&system_call, f->esp, sizeof(int));

  switch (system_call) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      int status;
      memcpy(&status, f->esp + 4, sizeof(int));
      exit(status);
      break;

    case SYS_WAIT:
      break;

    case SYS_CREATE:
      break;

    case SYS_REMOVE:
      break;

    case SYS_OPEN:
      break;

    case SYS_FILESIZE:
      break;

    case SYS_READ:
      break;

    case SYS_WRITE:
      int fd;
      void *buffer;
      unsigned size;
      memcpy(&fd, f->esp + 4, sizeof(fd));
      memcpy(&buffer, f->esp + 8, sizeof(buffer));
      memcpy(&size, f->esp + 12, sizeof(size));
      write(fd, buffer, size);
      break; 
    
    case SYS_SEEK:
      break;

    case SYS_TELL:
      break;

    case SYS_CLOSE:
      break;
  }

}

void
halt (void) 
{
  shutdown_power_off();
}

void
exit (int status) 
{
  /* Sets exit status of current process in child_bond struct. */
  process_set_exit_status(status);
  /* Thread exit calls process_exit, 
     therefore the thread and process are exitted. */
  thread_exit();
}

pid_t
exec (const char *cmd_line) 
{
  return (pid_t) process_execute(cmd_line);
}

int
wait (pid_t pid) 
{
  // Incomplete
  return process_wait(pid);
}

bool 
create (const char *file, unsigned initial_size) 
{
  // Incomplete
  return false;
}

bool
remove (const char *file) 
{
  // Incomplete
  return false;
}

int
open (const char *file) 
{
  // Incomplete
  return -1;
}

int 
filesize (int fd) 
{
  // Incomplete
  return -1;
}

int 
read (int fd, void *buffer, unsigned size) 
{
  // Incomplete
  return -1;
}

struct file *
get_file(int fd) {
  struct list *open_files = &thread_current()->open_files;
  for (struct list_elem *e = list_begin(open_files); e != list_end(open_files); e = list_next(e)) {
    struct open_file *of = list_entry(e, struct open_file, elem);
    if (fd == of->fd) {
      return of->fd;
    }
  }
  return NULL;
}

int
write (int fd, const void *buffer, unsigned size)
{
  
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  } else {
    lock_acquire(&filesystem_lock);
    struct file *file = get_file(fd);
    if (file != NULL) {
      int n = file_write(file, buffer, size);
      lock_release(&filesystem_lock);
      return n;
    } else {
      lock_release(&filesystem_lock);
      thread_exit();
    }
  }
}

void
seek (int fd, unsigned position) 
{
  // Incomplete
}

unsigned
tell (int fd)
{
  // Incomplete
  return 0;
}

void
close (int fd) 
{
  // Incomplete
}