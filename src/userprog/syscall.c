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
#include "threads/vaddr.h"
#include "threads/palloc.h"

#define NUM_CALLS 13

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
  int system_call = read_write_user(&system_call, f->esp, sizeof(system_call));

  if (!system_call || system_call < 0 || system_call >= NUM_CALLS)
    thread_exit ();

  switch (system_call) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      int status;
      read_write_user(&status, f->esp + 4, sizeof(int));
      exit(status);
      break;

    case SYS_WAIT:
      int tid;
      if (!read_write_user(f->esp + 4, &tid, sizeof (tid)))
        thread_exit ();
      f->eax = wait (tid);
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

      if (!(read_write_user(f->esp + 4, &fd, sizeof (fd)) && read_write_user(f->esp + 8, &buffer, sizeof (buffer))
          && read_write_user(f->esp + 12, &size, sizeof (size))))
        thread_exit ();
      if (get_user (buffer) == -1 || get_user (buffer + size - 1) == -1)
        thread_exit ();

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
wait (int tid)
{
  return process_wait (tid);
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

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  if (!is_user_vaddr (uaddr))
  {
    return -1;
  }
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
UDST must be below PHYS_BASE.
Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a"(error_code), "=m"(*udst)
      : "q"(byte));
  return error_code != -1;
}

/* Copies from UADDR to dst using the get_user() function.
   True on success, false on segfault. */
static bool
read_write_user (void *src, void *dst, size_t bytes)
{
  int32_t value;
  for (size_t i = 0; i < bytes; i++)
  {
    value = get_user(src + i);
    if (value == -1)
      return false; // invalid memory access.
    *(char *)(dst + i) = value & 0xff;
  }
  return true;
}