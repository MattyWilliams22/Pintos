#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int pid_t;

void syscall_init (void);

void halt (void);
void exit (int status);
pid_t exec (const char *file);
int wait (int);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

struct file *get_file(int fd);

static bool read_write_user (void *src, void *dst, size_t bytes);
static bool put_user (uint8_t *udst, uint8_t byte);
static int get_user (const uint8_t *uaddr);

#endif /* userprog/syscall.h */
