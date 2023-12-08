#include "userprog/syscall.h"
#include <stdio.h>
#include <inttypes.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/mmap.h"

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  int syscall_num;
  if (!read_write_user (f->esp, &syscall_num, sizeof (syscall_num)))
    thread_exit ();
  if (syscall_num < 0 || syscall_num >= NUM_SUB_HANDLERS)
    thread_exit ();

  /* Save user stack pointer so that we can deal with stack growth happening
     from within system calls. */
  thread_current ()->esp = f->esp;

  (*sub_handlers[syscall_num]) (f);
}

static void
sys_halt (struct intr_frame *f UNUSED)
{
  shutdown_power_off ();
}

static void
sys_exit (struct intr_frame *f)
{
  int status;
  if (!read_write_user (f->esp + 4, &status, sizeof (status)))
    thread_exit ();

  process_set_exit_status (status);
  thread_exit ();
}

static void
sys_exec (struct intr_frame *f)
{
  void *cmdline_uaddr;
  if (!read_write_user (f->esp + 4, &cmdline_uaddr, sizeof (cmdline_uaddr)))
    thread_exit ();

  char *cmdline_copy = palloc_get_page (0);
  if (cmdline_copy == NULL)
    thread_exit ();
  if (safe_user_copy (cmdline_uaddr, cmdline_copy, PGSIZE) == -1)
    {
      palloc_free_page (cmdline_copy);
      thread_exit ();
    }

  f->eax = process_execute (cmdline_copy);
  palloc_free_page (cmdline_copy);
}

static void
sys_wait (struct intr_frame *f)
{
  tid_t tid;
  if (!read_write_user (f->esp + 4, &tid, sizeof (tid)))
    thread_exit ();

  f->eax = process_wait (tid);
}

static void
sys_create (struct intr_frame *f)
{
  char *file;
  unsigned initial_size;
  if (!read_write_user (f->esp + 4, &file, sizeof (file))
      || !read_write_user (f->esp + 8, &initial_size, sizeof (initial_size)))
    thread_exit ();

  char *file_copy = palloc_get_page (0);
  if (file_copy == NULL)
    thread_exit ();
  if (safe_user_copy (file, file_copy, PGSIZE) == -1)
    {
      palloc_free_page (file_copy);
      thread_exit ();
    }

  acquire_filesystem_lock ();
  f->eax = filesys_create (file, initial_size);
  release_filesystem_lock ();
  palloc_free_page (file_copy);
}

static void
sys_remove (struct intr_frame *f)
{
  char *file;
  if (!read_write_user (f->esp + 4, &file, sizeof (file)))
    thread_exit ();

  char *file_copy = palloc_get_page (0);
  if (file_copy == NULL)
    thread_exit ();
  if (safe_user_copy (file, file_copy, PGSIZE) == -1)
    {
      palloc_free_page (file_copy);
      thread_exit ();
    }

  acquire_filesystem_lock ();
  f->eax = filesys_remove (file);
  release_filesystem_lock ();
  palloc_free_page (file_copy);
}

static void
sys_open (struct intr_frame *f)
{
  char *file;
  if (!read_write_user (f->esp + 4, &file, sizeof (file)))
    thread_exit ();

  char *file_copy = palloc_get_page (0);
  if (file_copy == NULL)
    thread_exit ();
  if (safe_user_copy (file, file_copy, PGSIZE) == -1)
    {
      palloc_free_page (file_copy);
      thread_exit ();
    }

  acquire_filesystem_lock ();
  f->eax = process_open_file (file_copy);
  release_filesystem_lock ();
  palloc_free_page (file_copy);
}

static void
sys_filesize (struct intr_frame *f)
{
  int fd;
  if (!read_write_user (f->esp + 4, &fd, sizeof (fd)))
    thread_exit ();

  acquire_filesystem_lock ();
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      release_filesystem_lock ();
      thread_exit ();
    }
  f->eax = file_length (file);
  release_filesystem_lock ();
}

static void
sys_read (struct intr_frame *f UNUSED)
{
  int fd;
  void *buffer;
  unsigned size;
  if (!read_write_user (f->esp + 4, &fd, sizeof (fd))
      || !read_write_user (f->esp + 8, &buffer, sizeof (buffer))
      || !read_write_user (f->esp + 12, &size, sizeof (size)))
    thread_exit ();

  if (get_user (buffer) == -1 || get_user (buffer + size - 1) == -1)
    thread_exit ();

  if (fd == 0)
    {
      for (unsigned i = 0; i < size; i++)
        ((char *) buffer)[i] = input_getc ();
      f->eax = size;
    }
  else
    {
      acquire_filesystem_lock ();
      struct file *file = process_get_file (fd);
      if (file == NULL)
        {
          release_filesystem_lock ();
          thread_exit ();
        }
      f->eax = file_read (file, buffer, size);
      release_filesystem_lock ();
    }
}

static void
sys_write (struct intr_frame *f)
{
  int fd;
  void *buffer;
  unsigned size;
  if (!(read_write_user (f->esp + 4, &fd, sizeof (fd))
        && read_write_user (f->esp + 8, &buffer, sizeof (buffer))
        && read_write_user (f->esp + 12, &size, sizeof (size))))
    thread_exit ();

  if (get_user (buffer) == -1 || get_user (buffer + size - 1) == -1)
    thread_exit ();

  if (fd == 1)
    {
      putbuf (buffer, size);
      f->eax = size;
      return;
    }
  else
    {
      acquire_filesystem_lock ();
      struct file *file = process_get_file (fd);
      if (file == NULL)
        {
          release_filesystem_lock ();
          thread_exit ();
        }
      f->eax = file_write (file, buffer, size);
      release_filesystem_lock ();
    }
}

static void
sys_seek (struct intr_frame *f UNUSED)
{
  int fd;
  unsigned position;
  if (!read_write_user (f->esp + 4, &fd, sizeof (fd))
      || !read_write_user (f->esp + 8, &position, sizeof (position)))
    thread_exit ();

  acquire_filesystem_lock ();
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      release_filesystem_lock ();
      thread_exit ();
    }
  file_seek (file, position);
  release_filesystem_lock ();
}

static void
sys_tell (struct intr_frame *f UNUSED)
{
  int fd;
  if (!read_write_user (f->esp + 4, &fd, sizeof (fd)))
    thread_exit ();

  acquire_filesystem_lock ();
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      release_filesystem_lock ();
      thread_exit ();
    }
  f->eax = file_tell (file);
  release_filesystem_lock ();
}

static void
sys_close (struct intr_frame *f)
{
  int fd;
  if (!read_write_user (f->esp + 4, &fd, sizeof (fd)))
    thread_exit ();

  acquire_filesystem_lock ();
  process_close_file (fd);
  release_filesystem_lock ();
}

static void
sys_mmap (struct intr_frame *f)
{
  int fd;
  void *addr;
  if (!read_write_user (f->esp + 4, &fd, sizeof (fd))
      || !read_write_user (f->esp + 8, &addr, sizeof (addr)))
    thread_exit ();

  f->eax = mmap (fd, addr);
}

static void
sys_munmap (struct intr_frame *f)
{
  mapid_t map_id;
  if (!read_write_user (f->esp + 4, &map_id, sizeof (map_id)))
    thread_exit ();

  munmap (map_id);
}

static int
get_user (const uint8_t *uaddr)
{
  if (!is_user_vaddr (uaddr))
    return -1;
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}

static bool
put_user (uint8_t *udst, uint8_t byte)
{
  if (!is_user_vaddr (udst))
    return false;
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a"(error_code), "=m"(*udst)
       : "q"(byte));
  return error_code != -1;
}

static bool
read_write_user (void *src, void *dst, size_t buf_size)
{
  for (size_t i = 0; i < buf_size; i++)
    {
      int value = get_user (src + i);
      if (value == -1)
        return false;
      ((char *) dst)[i] = value & 0xff;
    }
  return true;
}

static int
safe_user_copy (void *src, char *dst, size_t buf_size)
{
  size_t i = 0;
  for (; i < buf_size; i++)
    {
      int value = get_user (src + i);
      if (value == -1)
        return -1;
      dst[i] = value & 0xff;
      if (dst[i] == 0)
        break;
    }
  if (i == buf_size)
    dst[--i] = 0;
  return i;
}
