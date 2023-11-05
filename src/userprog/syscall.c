#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}

void
halt (void) {
  shutdown_power_off();
}

void
exit (int status) 
{
  // Incomplete
}

pid_t
exec (const char *cmd_line) 
{
  // Must split up cmd_line to get file name
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

int
write (int fd, const void *buffer, unsigned size)
{
  // Incomplete
  return -1;
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