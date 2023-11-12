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
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  bool read_safely = true;

  int system_call;
  read_safely &= read_write_user(f->esp, &system_call, sizeof(system_call));
  if (!read_safely) {
    thread_exit();
  }

  if (system_call < 0 || system_call >= NUM_CALLS)
    thread_exit ();

  int status;
  char *cmd_line;
  int tid;
  char *file;
  char *file_copy;
  unsigned initial_size;
  void *buffer;
  unsigned size;
  int fd;
  unsigned position;
  switch (system_call) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      read_safely &= read_write_user(f->esp + 4, &status, sizeof(int));
      if (!read_safely) {
        thread_exit();
      }
      exit(status);
      break;

    case SYS_EXEC:
      read_safely &= read_write_user(f->esp + 4, &cmd_line, sizeof(char *));
      if (!read_safely) {
        thread_exit();
      }
      f->eax = exec(cmd_line);
      break;

    case SYS_WAIT:
      read_safely &= read_write_user(f->esp + 4, &tid, sizeof(tid));
      if (!read_safely) {
        thread_exit();
      }
      f->eax = wait(tid);
      break;

    case SYS_CREATE:
      read_safely &= read_write_user(f->esp + 4, &file, sizeof(file));
      read_safely &= read_write_user(f->esp + 8, &initial_size, sizeof(initial_size));
      if (!read_safely) {
        thread_exit();
      }

      file_copy = palloc_get_page (0);
      if (file_copy == NULL)
        thread_exit ();
      if (safe_user_copy (file, file_copy, PGSIZE) == -1)
      {
        palloc_free_page (file_copy);
        thread_exit ();
      }
      f->eax = create(file, initial_size);
      palloc_free_page (file_copy);
      break;

    case SYS_REMOVE:
      read_safely &= read_write_user(f->esp + 4, &file, sizeof(file));
      if (!read_safely) {
        thread_exit();
      }
      file_copy = palloc_get_page (0);
      if (file_copy == NULL)
        thread_exit ();
      if (safe_user_copy (file, file_copy, PGSIZE) == -1)
      {
        palloc_free_page (file_copy);
        thread_exit ();
      }
      f->eax = remove(file);
      palloc_free_page (file_copy);
      break;

    case SYS_OPEN:
      read_safely &= read_write_user(f->esp + 4, &file, sizeof(file));
      if (!read_safely) {
        thread_exit();
      }
      file_copy = palloc_get_page (0);
      if (file_copy == NULL)
        thread_exit ();
      if (safe_user_copy (file, file_copy, PGSIZE) == -1)
      {
        palloc_free_page (file_copy);
        thread_exit ();
      }
      f->eax = open(file);
      palloc_free_page (file_copy);
      break;

    case SYS_FILESIZE:
      read_safely &= read_write_user(f->esp + 4, &fd, sizeof(fd));
      if (!read_safely) {
        thread_exit();
      }
      f->eax = filesize(fd);
      break;

    case SYS_READ:
      read_safely &= read_write_user(f->esp + 4, &fd, sizeof(fd));
      read_safely &= read_write_user(f->esp + 8, &buffer, sizeof(buffer));
      read_safely &= read_write_user(f->esp + 12, &size, sizeof(size));
      if (!read_safely || get_user (buffer) == -1 || get_user (buffer + size - 1) == -1) {
        thread_exit();
      }
      f->eax = read(fd, buffer, size);
      break;

    case SYS_WRITE:
      read_safely &= read_write_user(f->esp + 4, &fd, sizeof(fd));
      read_safely &= read_write_user(f->esp + 8, &buffer, sizeof(buffer));
      read_safely &= read_write_user(f->esp + 12, &size, sizeof(size));
      if (!read_safely || get_user (buffer) == -1 || get_user (buffer + size - 1) == -1) {
        thread_exit();
      }
      if (get_user(buffer) == -1 || get_user(buffer + size - 1) == -1)
        thread_exit ();
      f->eax = write(fd, buffer, size);
      break; 
    
    case SYS_SEEK:
      read_safely &= read_write_user(f->esp + 4, &fd, sizeof(fd));
      read_safely &= read_write_user(f->esp + 8, &position, sizeof(position));
      if (!read_safely) {
        thread_exit();
      }
      seek(fd, position);
      break;

    case SYS_TELL:
      read_safely &= read_write_user(f->esp + 4, &fd, sizeof(fd));
      if (!read_safely) {
        thread_exit();
      }
      f->eax = tell(fd);
      break;

    case SYS_CLOSE:
      read_safely &= read_write_user(f->esp + 4, &fd, sizeof(fd));
      if (!read_safely) {
        thread_exit();
      }
      close(fd);
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
  acquire_filesystem_lock();
  bool success = filesys_create(file, initial_size);
  release_filesystem_lock();
  return success;
}

bool
remove (const char *file) 
{
  acquire_filesystem_lock();
  bool success = filesys_remove(file);
  release_filesystem_lock();
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
  acquire_filesystem_lock();
  int fd = open_file_by_name(file);
  release_filesystem_lock();
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
  acquire_filesystem_lock();
  struct file *file = get_open_file(fd);
  if (file != NULL) {
    int file_len = (int) file_length(file);
    release_filesystem_lock();
    return file_len;
  } else {
    release_filesystem_lock();
    thread_exit();
  }
}

int 
read (int fd, void *buffer, unsigned size) 
{
  if (fd == 0) {
    for (unsigned i = 0; i < size; i++) {
      ((char *) buffer)[i] = input_getc();
    }
    return size;
  } else {
    acquire_filesystem_lock();
    struct file *file = get_open_file(fd);
    if (file != NULL) {
      int bytes_read = file_read(file, buffer, size);
      release_filesystem_lock();
      return bytes_read;
    } else {
      release_filesystem_lock();
      thread_exit();
    }
  }
}

int
write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  } else {
    acquire_filesystem_lock();
    struct file *file = get_open_file(fd);
    if (file != NULL) {
      int bytes_written = file_write(file, buffer, size);
      release_filesystem_lock();
      return bytes_written;
    } else {
      release_filesystem_lock();
      thread_exit();
    }
  }
}

void
seek (int fd, unsigned position) 
{
  acquire_filesystem_lock();
  struct file *file = get_open_file(fd);
  if (file != NULL) {
    file_seek(file, position);
    release_filesystem_lock();
  } else {
    release_filesystem_lock();
    thread_exit();
  }
}

unsigned
tell (int fd)
{
  acquire_filesystem_lock();
  struct file *file = get_open_file(fd);
  if (file != NULL) {
    unsigned n = file_tell(file);
    release_filesystem_lock();
    return n;
  } else {
    release_filesystem_lock();
    thread_exit();
  }
}

void
close_file_by_fd(int fd) 
{
  struct list *open_files = &thread_current ()->open_files;
  for (struct list_elem *e = list_begin(open_files); e != list_end(open_files); e = list_next(e))
  {
    struct open_file *open_file = list_entry(e, struct open_file, elem);
    if (open_file->fd == fd)
    {
      list_remove(&open_file->elem);
      file_close(open_file->file);
      free(open_file);
      return;
    }
  }
}

void
close (int fd) 
{
  acquire_filesystem_lock();
  close_file_by_fd(fd);
  release_filesystem_lock();
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

int
safe_user_copy (void *src, char *dst, size_t buffer_size)
{
  size_t i = 0;
  for (; i < buffer_size; i++)
  {
    int value = get_user (src + i);
    if (value == -1)
      return -1;
    dst[i] = value & 0xff;
    if (dst[i] == 0)
      break;
  }
  if (i == buffer_size)
    dst[--i] = 0;
  return i;
}