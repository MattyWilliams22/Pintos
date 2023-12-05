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
#include "devices/input.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "vm/mmap.h"

#define NUM_CALLS 15

static int get_user (const uint8_t *uaddr);
static bool read_write_user (void *src, void *dst, size_t bytes);
static int safe_user_copy (void *src, char *dst, size_t buffer_size);

static void syscall_handler (struct intr_frame *);
int open_file_by_name(char *file_name);
int open (char *file);
struct file *get_open_file(int fd);
void close_file_by_fd(int fd);
struct file *get_file(int fd);

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
  char *cmd_line_copy;
  pid_t pid;
  char *file;
  char *file_copy;
  unsigned initial_size;
  void *buffer;
  unsigned size;
  int fd;
  unsigned position;
  void *addr;
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

      cmd_line_copy = palloc_get_page (0);
      if (cmd_line_copy == NULL)
        thread_exit ();
      if (safe_user_copy (cmd_line, cmd_line_copy, PGSIZE) == -1)
      {
        palloc_free_page (cmd_line_copy);
        thread_exit ();
      }
      f->eax = exec(cmd_line_copy);
      palloc_free_page (cmd_line_copy);
      break;

    case SYS_WAIT:
      read_safely &= read_write_user(f->esp + 4, &pid, sizeof(pid));
      if (!read_safely) {
        thread_exit();
      }
      f->eax = wait(pid);
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
    
    case SYS_MMAP:
      read_safely &= read_write_user(f->esp + 4, &fd, sizeof(fd));
      read_safely &= read_write_user(f->esp + 8, &addr, sizeof(addr));
      if (!read_safely) {
        thread_exit();
      }
      f->eax = mmap(fd, addr);
      break;

    case SYS_MUNMAP:
      mapid_t mapid;
      read_safely &= read_write_user(f->esp + 4, &mapid, sizeof(mapid));
      if (!read_safely) {
        thread_exit();
      }
      munmap(mapid);
      break;
  }

}

/* Terminates Pintos. */
void
halt (void) 
{
  shutdown_power_off();
}

/* Terminates the current user program. */
void
exit (int status) 
{
  /* Sets exit status of current process in child_bond struct. */
  process_set_exit_status(status);
  /* Thread exit calls process_exit, 
     therefore the thread and process are exitted. */
  thread_exit();
}

/* Runs the executable given in cmd_line. */
pid_t
exec (char *cmd_line) 
{
  return (pid_t) process_execute(cmd_line);
}

/* Waits until child process (tid) terminates, 
   then returns the exit status of the child process. */
int
wait (pid_t pid)
{
  return process_wait(pid);
}

/* Creates a new file called file, initially initial_size bytes in size. */
bool 
create (char *file, unsigned initial_size) 
{
  acquire_filesystem_lock();
  bool success = filesys_create(file, initial_size);
  release_filesystem_lock();
  return success;
}

/* Deletes the file called file from the file system. */
bool
remove (char *file) 
{
  acquire_filesystem_lock();
  bool success = filesys_remove(file);
  release_filesystem_lock();
  return success;
}

/* Compares two files based on their fd values. */
static bool
sort_files_by_fd(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct open_file *file_a = list_entry(a, struct open_file, elem);
  struct open_file *file_b = list_entry(b, struct open_file, elem);
  return file_a->fd < file_b->fd;
}

/* Opens a file with the name file_name and adds it to this threads list of open files. */
int 
open_file_by_name(char *file_name)
{
  struct open_file *new_file;
  new_file = (struct open_file *) malloc(sizeof(struct open_file));
  if (new_file == NULL) {
    return -1;
  }
  
  struct file *file = filesys_open(file_name);
  if (file == NULL) {
    free(new_file);
    return -1;
  }

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

/* Opens the file called file. */
int
open (char *file) 
{
  acquire_filesystem_lock();
  int fd = open_file_by_name(file);
  release_filesystem_lock();
  return fd;
}

/* Gets the file struct of the file open as fd. */
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

/* Returns the size, in bytes, of the file open as fd. */
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

/* Reads size bytes from buffer into the open file fd. 
   If fd == 0, reads keyboard input instead. */
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

/* Writes size bytes from buffer to the open file fd. 
   If fd == 1, writes all of buffer to the console. */
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

/* Changes the next byte to be read or written to in open file fd to position. */
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

/* Returns the position of the next byte to be read or written in open file fd,
   expressed in bytes from the beginning of the file. */
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

/* Closes the open file fd. */
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

/* Closes file descriptor fd. */
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
int
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

/* Copies from UADDR to dst using the get_user() function.
   True on success, false on segfault. */
bool
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

/* Copies buffer_size bytes from src to dst, 
   checking that each byte is in user memory. */
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