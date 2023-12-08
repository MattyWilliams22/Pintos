#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int pid_t;

void syscall_init (void);

typedef void (*sub_handler) (struct intr_frame *);

#define NUM_SUB_HANDLERS 15
static const sub_handler sub_handlers[NUM_SUB_HANDLERS]
    = { &sys_halt,   &sys_exit, &sys_exec,     &sys_wait, &sys_create,
        &sys_remove, &sys_open, &sys_filesize, &sys_read, &sys_write,
        &sys_seek,   &sys_tell, &sys_close,    &sys_mmap, &sys_munmap };


static void syscall_handler (struct intr_frame *f);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte) UNUSED;
static bool read_write_user (void *src, void *dst, size_t buf_size);
static int safe_user_copy (void *src, char *dst, size_t buf_size);

static void sys_halt (struct intr_frame *f);
static void sys_exit (struct intr_frame *f);
static void sys_exec (struct intr_frame *f);
static void sys_wait (struct intr_frame *f);
static void sys_create (struct intr_frame *f);
static void sys_remove (struct intr_frame *f);
static void sys_open (struct intr_frame *f);
static void sys_filesize (struct intr_frame *f);
static void sys_read (struct intr_frame *f);
static void sys_write (struct intr_frame *f);
static void sys_seek (struct intr_frame *f);
static void sys_tell (struct intr_frame *f);
static void sys_close (struct intr_frame *f);
static void sys_mmap (struct intr_frame *f);
static void sys_munmap (struct intr_frame *f);

#endif /* userprog/syscall.h */
