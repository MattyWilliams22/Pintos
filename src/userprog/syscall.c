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
#include "threads/malloc.h"

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
int open_file_by_name(const char *file_name);
int open (const char *file);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init(&filesystem_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int system_call;
  read_write_user(f->esp, &system_call, sizeof(system_call));

  if (system_call < 0 || system_call >= NUM_CALLS)
    thread_exit ();

  int status;
  char *cmd_line;
  int tid;
  char *file;
  unsigned initial_size;
  void *buffer;
  unsigned size;
  int fd;
  switch (system_call) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      read_write_user(f->esp + 4, &status, sizeof(int));
      exit(status);
      break;

    case SYS_EXEC:
      read_write_user(f->esp + 4, &cmd_line, sizeof(char *));
      f->eax = exec(cmd_line);
      break;

    case SYS_WAIT:
      if (!read_write_user(f->esp + 4, &tid, sizeof(tid)))
        thread_exit ();
      f->eax = wait(tid);
      break;

    case SYS_CREATE:
      read_write_user(f->esp + 4, &file, sizeof(file));
      read_write_user(f->esp + 8, &initial_size, sizeof(initial_size));
      f->eax = create(file, initial_size);
      break;

    case SYS_REMOVE:
      read_write_user(f->esp + 4, &file, sizeof(file));
      f->eax = remove(file);
      break;

    case SYS_OPEN:
      read_write_user(f->esp + 4, &file, sizeof(file));
      f->eax = open(file);
      break;

    case SYS_FILESIZE:
      read_write_user(f->esp + 4, &fd, sizeof(fd));
      f->eax = filesize(fd);
      break;

    case SYS_READ:
      read_write_user(f->esp + 4, &fd, sizeof(fd));
      read_write_user(f->esp + 8, &buffer, sizeof(buffer));
      read_write_user(f->esp + 12, &size, sizeof(size));
      f->eax = read(fd, buffer, size);
      break;

    case SYS_WRITE:
      if (!(read_write_user(f->esp + 4, &fd, sizeof(fd)) && read_write_user(f->esp + 8, &buffer, sizeof(buffer))
          && read_write_user(f->esp + 12, &size, sizeof(size))))
        thread_exit ();
      if (get_user(buffer) == -1 || get_user(buffer + size - 1) == -1)
        thread_exit ();
      f->eax = write(fd, buffer, size);
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
  return process_wait(tid);
}

bool 
create (const char *file, unsigned initial_size) 
{
  lock_acquire(&filesystem_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesystem_lock);
  return success;
}

bool
remove (const char *file) 
{
  lock_acquire(&filesystem_lock);
  bool success = filesys_remove(file);
  lock_release(&filesystem_lock);
  return success;
}

static bool
sort_files_by_fd(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct open_file *file_a = list_entry(a, struct open_file, elem);
  struct open_file *file_b = list_entry(b, struct open_file, elem);
  return file_a->fd < file_b->fd;
}

int 
open_file_by_name(const char *file_name)
{
  struct open_file *new_file;
  new_file = (struct open_file *) malloc(sizeof(struct open_file));
  struct file *file = filesys_open(file_name);
  struct list *open_files = &thread_current()->open_files;
  struct list_elem *max_fd = list_max(open_files, sort_files_by_fd, NULL);
  int fd = list_entry(max_fd, struct open_file, elem)->fd + 1;
  if (fd <= STDOUT_FILENO) {
    fd = STDOUT_FILENO + 1;
  }
  new_file->fd = fd;
  new_file->file = file;
  list_push_back(open_files, &new_file->elem);
  return fd;
}

int
open (const char *file) 
{
  lock_acquire(&filesystem_lock);
  int fd = open_file_by_name(file);
  lock_release(&filesystem_lock);
  return fd;
}

struct file *
get_open_file(int fd) {
  struct list *open_files = &thread_current()->open_files;
  for (struct list_elem *e = list_begin(open_files); e != list_end(open_files); e = list_next(e)) {
    struct open_file *of = list_entry(e, struct open_file, elem);
    if (fd == of->fd) {
      return of->file;
    }
  }
  return NULL;
}

int 
filesize (int fd) 
{
  lock_acquire(&filesystem_lock);
  struct file *file = get_open_file(fd);
  if (file != NULL) {
    int file_len = (int) file_length(file);
    lock_release(&filesystem_lock);
    return file_len;
  } else {
    lock_release(&filesystem_lock);
    thread_exit();
  }
}

int 
read (int fd, void *buffer, unsigned size) 
{
  // Incomplete
  return -1;
}

int
write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  } else {
    lock_acquire(&filesystem_lock);
    struct file *file = get_open_file(fd);
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