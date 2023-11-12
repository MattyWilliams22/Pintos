#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

void process_set_exit_status(int exit_status);

tid_t process_execute (const char *cmd_line);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

int failure (struct child_bond *child_bond, char *cmdline, struct process_setup_params *params);


#endif /* userprog/process.h */
