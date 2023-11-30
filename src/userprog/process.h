#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

void process_set_exit_status(int exit_status);

tid_t process_execute (char *cmd_line);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void filesystem_lock_init (void);
void acquire_filesystem_lock (void);
void release_filesystem_lock (void);

/* Element used to add a file to a list of open files. */
struct open_file
{
  struct list_elem elem;  /* List element used in thread->open_files list. */
  int fd;                 /* File descriptor. */
  struct file *file;      /* File struct. */
};

struct file *process_get_file(int fd);

#endif /* userprog/process.h */
