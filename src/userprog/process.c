#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/frame.h"
#include "vm/mmap.h"
#include "vm/page.h"

static thread_func start_process NO_RETURN;
static struct file *load (const char *cmdline, void (**eip) (void), void **esp);
static void process_lose_connection(struct child_bond *child_bond);

/* Lock used to restrict access to the file system. */
struct lock filesystem_lock;

/* Struct used to track return value of child processes. */
struct child_bond
{
  tid_t child_tid;        /* The tid of the child process. */
  int exit_status;        /* The return value of the child process, default is -1. */
  struct list_elem elem;  /* List element used in thread->child_bonds list. */
  struct semaphore sema;  /* Semaphore used to wait for the child process. */
  int connections;        /* Holds the number of active processes connected to this bond.
                             The bond will be freed when connections becomes 0. */
  struct lock lock;       /* Lock used to control access to the bond. */
};

/* Struct used to pass parameters required to set up a process. */
struct process_setup_params
{
  struct child_bond *child_bond;
  char *cmd_line;
};

/* Locks the file system. */
void
acquire_filesystem_lock (void)
{
  lock_acquire (&filesystem_lock);
}

/* Unlocks the file system. */
void
release_filesystem_lock (void)
{
  lock_release (&filesystem_lock);
}

/* Initialises the filesystem lock. */
void
init_filesystem_lock (void)
{
  lock_init (&filesystem_lock);
}


/* Decrements the number of connections to the child_bond struct, 
   as long as the current thread holds the lock for this bond. 
   Frees the bond if the number of connections becomes 0. */
static void
process_lose_connection(struct child_bond *child_bond) {
  if (child_bond->lock.holder != thread_current() || child_bond->connections < 0) {
    return;
  }

  child_bond->connections--;

  if (child_bond->connections == 0) {
    lock_destroy(&child_bond->lock);
    free(child_bond);
    return;
  }
  
  lock_release(&child_bond->lock);
}

/* Passes exit status of process to child_bond struct. */
void
process_set_exit_status(int exit_status) {
  if (thread_current()->child_bond == NULL) {
    return;
  }
  thread_current()->child_bond->exit_status = exit_status;
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (char *cmd_line) 
{
  char *cmd_line_copy = NULL;
  struct child_bond *child_bond = NULL;
  struct process_setup_params *setup_params = NULL;

  /* Make a copy of cmd_line.
     Otherwise there's a race between the caller and load(). */
  cmd_line_copy = palloc_get_page (0);
  if (cmd_line_copy == NULL)
    goto fail;
  strlcpy (cmd_line_copy, cmd_line, PGSIZE);

  /* Initialise child_bond struct. */
  child_bond = (struct child_bond *) malloc(sizeof(struct child_bond));
  if (child_bond == NULL) 
  {
    goto fail;
  }
  child_bond->child_tid = TID_ERROR;
  child_bond->exit_status = -1;
  list_push_back(&thread_current()->child_bonds, &child_bond->elem);
  sema_init(&child_bond->sema, 0);
  child_bond->connections = 1;
  lock_init(&child_bond->lock);

  /* Create struct containing parameters required to set up a process. */
  setup_params = (struct process_setup_params *) malloc(sizeof(struct process_setup_params));
  if (setup_params == NULL) {
    goto fail;
  }
  setup_params->child_bond = child_bond;
  setup_params->cmd_line = cmd_line_copy;


  /* Where the strtrok_r function will carry on from after getting program name. */
  char *continue_from;

  /* Using strtok_r to get the command to run. */
  char *program_name;
  program_name = strtok_r(cmd_line, " ", &continue_from);
  if (program_name == NULL) {
    goto fail;
  }

  /* Check that the file exists. */
  struct file *file = NULL;
  acquire_filesystem_lock();
  file = filesys_open (program_name);
  if (file == NULL) 
  {
    printf ("load: %s: open failed\n", program_name);
    goto fail; 
  }
  file_close (file);
  release_filesystem_lock();

  /* Create a new thread to execute FILE_NAME. */
  tid_t tid = thread_create (program_name, PRI_DEFAULT, start_process, setup_params);
  if (tid == TID_ERROR)
    goto fail;

  /* Pause until child has set value of child_bond->tid. */
  sema_down(&child_bond->sema);
  if (child_bond->child_tid == TID_ERROR) {
    goto fail;
  }

  palloc_free_page (cmd_line_copy);
  free(setup_params);

  return tid;
fail:
  if (cmd_line_copy != NULL)
    palloc_free_page (cmd_line_copy);
  if (child_bond != NULL)
  {
    list_remove (&child_bond->elem);
    free (child_bond);
  }
  if (setup_params != NULL)
    free (setup_params);

  return TID_ERROR;
}

/* Pushes size bytes from src onto the stack. */
static bool
stack_push (void **esp, const void *src, size_t size)
{
  if (*esp < PHYS_BASE - STACK_LIMIT + size)
    return false;
  *esp -= size;
  memcpy (*esp, src, size);
  return true;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *setup_params_v)
{
  struct thread *curr_thread = thread_current ();
  struct process_setup_params *setup_params = (struct process_setup_params *) setup_params_v;
  struct intr_frame if_;
  char **argument_values = NULL;

  curr_thread->is_user = true;
  curr_thread->exec_file = NULL;
  curr_thread->esp = NULL;
  curr_thread->child_bond = NULL;


  if(!init_pt (&curr_thread->page_table))
    goto fail;

  process_activate ();

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* Argument values and count. */
  size_t argument_count = strtok_count (setup_params->cmd_line, " ");
  if (argument_count == 0)
    goto fail;
  argument_values = malloc (argument_count * sizeof (char **));
  if (argument_values == NULL)
    goto fail;

  /* Set up stack. */ 
  char *curr_token;
  char *continue_from;
  size_t i = 0;
  for (curr_token = strtok_r (setup_params->cmd_line, " ", &continue_from); curr_token != NULL;
       curr_token = strtok_r (NULL, " ", &continue_from), i++)
  {
    ASSERT (i < argument_count);

    if (i == 0 && (curr_thread->exec_file = load (curr_token, &if_.eip, &if_.esp)) == NULL)
      goto fail;

    /* Copy arg onto stack. */
    if (!stack_push (&if_.esp, curr_token, strlen (curr_token) + 1))
      goto fail;

    argument_values[i] = if_.esp;
  }

  /* Word align. */ 
  if_.esp = (void *) (((uintptr_t) if_.esp) & 0xfffffffc);

  /* Insert null. */
  void *null = NULL;
  if (!stack_push (&if_.esp, &null, sizeof (null))
      || !stack_push (&if_.esp, argument_values, argument_count * sizeof (char *)))
    goto fail;

  void *argument_values_stack = if_.esp;
  uint32_t argument_count_stack = argument_count;
  if (!stack_push (&if_.esp, &argument_values_stack, sizeof (argument_values_stack))
      || !stack_push (&if_.esp, &argument_count_stack, sizeof (argument_count_stack))
      || !stack_push (&if_.esp, &null, sizeof (null)))
    goto fail;

  /* Clean up. */
  free (argument_values);

  /* Adjust values of child_bond and wake up parent using semaphore. */
  curr_thread->child_bond = setup_params->child_bond;
  curr_thread->child_bond->child_tid = curr_thread->tid;
  curr_thread->child_bond->connections++;
  sema_up(&curr_thread->child_bond->sema);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();

fail:
  if (argument_values != NULL)
    free (argument_values);

  ASSERT (setup_params->child_bond->child_tid == TID_ERROR);
  sema_up (&curr_thread->child_bond->sema);

  thread_exit ();
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status. 
 * If it was terminated by the kernel (i.e. killed due to an exception), 
 * returns -1.  
 * If TID is invalid or if it was not a child of the calling process, or if 
 * process_wait() has already been successfully called for the given TID, 
 * returns -1 immediately, without waiting.
 * 
 * This function will be implemented in task 2.
 * For now, it does nothing. */
int
process_wait (tid_t child_tid)
{
  struct thread *current = thread_current();
  /* Iterate through the parent's list of child bonds. */
  for (struct list_elem *e = list_begin(&current->child_bonds); e != list_end (&current->child_bonds); e = list_next(e))
  {
    struct child_bond *bond = list_entry(e, struct child_bond, elem);
    if (bond->child_tid == child_tid)
    {
      /* Down semaphore with the child bond with the correct tid. 
         Will continue execution when child terminates and ups semaphore. */
      sema_down(&bond->sema);
      /* The exit status will be set by the child before upping the semaphore. */
      int exit_status = bond->exit_status;
      /* Remove the bond from the parent's list of bonds 
         and break the connection witht the bond. */
      list_remove(e);
      lock_acquire(&bond->lock);
      process_lose_connection(bond);
      return exit_status;
    }
  }
  /* If no bond was found with the corresponding tid, return -1 without waiting. */
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  free_pt (&cur->page_table);

  /* Write error message to console and break connection with child_bond. */
  if (cur->child_bond != NULL) {
    printf("%s: exit(%d)\n", cur->name, cur->child_bond->exit_status);

    sema_up(&cur->child_bond->sema);
    lock_acquire(&cur->child_bond->lock);
    process_lose_connection(cur->child_bond);
  }

  /* Let go of all connections to bonds with children. */
  struct list_elem *next;
  for (struct list_elem *e = list_begin(&cur->child_bonds);
       e != list_end(&cur->child_bonds); e = next)
    {
      struct child_bond *child = list_entry(e, struct child_bond, elem);
      next = list_next(e);
      lock_acquire(&child->lock);
      process_lose_connection(child);
    }

  /* Check if filesystem lock is held, if not, acquire the lock. */
  if (!lock_held_by_current_thread(&filesystem_lock)) {
    acquire_filesystem_lock ();
  }

  /* If open files, close and free memory. */ 
  for (struct list_elem *e = list_begin (&cur->open_files);
       e != list_end (&cur->open_files); e = next)
  {
    struct open_file *entry = list_entry (e, struct open_file, elem);
    file_close (entry->file);
    next = list_next (e);
    free (entry);
  }

  /* Close all memory mapped files. */
  for (struct list_elem *e = list_begin (&cur->mapped_files);
       e != list_end (&cur->mapped_files); e = next)
  {
    struct mapped_file *entry = list_entry (e, struct mapped_file, elem);
    file_close (entry->file);
    next = list_next (e);
    free (entry);
  }


  if (cur->exec_file != NULL)
    file_close (cur->exec_file);

  release_filesystem_lock ();
}



/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  activate_pt (&t->page_table);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
struct file *
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  acquire_filesystem_lock ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  file_deny_write(file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  if (!success && file != NULL)
    file_close (file);

  release_filesystem_lock ();
  return success ? file : NULL;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes + zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      
      /* Check if virtual page already allocated */
      struct thread *t = thread_current ();
      struct page_table *pt = &t->page_table;

      bool res = (page_read_bytes == 0) ? 
                create_zero_page (pt, upage, writable)
                : create_file_page (pt, upage, file, ofs, page_read_bytes, writable, false);
      if (!res)
        return false;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  *esp = PHYS_BASE;
  return true;
}

/* Get the open file specified by fd from the current thread's list of open files. */
struct file* 
process_get_file(int fd) 
{
  struct list *files = &thread_current ()->open_files;
  for (struct list_elem *e = list_begin (files); e != list_end (files); e = list_next (e))
  {
    struct open_file *entry = list_entry (e, struct open_file, elem);
    if (entry->fd == fd)
      return entry->file;
  }

  return NULL;
}

/* Compare the fd value of two open files. */
static bool
compare_files (const struct list_elem *a, const struct list_elem *b,
               void *aux UNUSED)
{
  struct open_file *fa = list_entry (a, struct open_file, elem);
  struct open_file *fb = list_entry (b, struct open_file, elem);
  return fa->fd < fb->fd;
}

/* Open file with the given filename and add it to current thread's list of open files. */
int
process_open_file (const char *file_name)
{
  struct open_file *open_file
      = (struct open_file *) malloc (sizeof (struct open_file));
  if (open_file == NULL)
    return -1;

  struct file *file = filesys_open (file_name);
  if (file == NULL)
  {
    free (open_file);
    return -1;
  }

  struct list *entries = &thread_current ()->open_files;
  struct list_elem *max = list_max (entries, compare_files, NULL);
  int fd = (max == list_end (entries))
                ? (STDOUT_FILENO + 1)
                : (list_entry (max, struct open_file, elem)->fd + 1);

  open_file->fd = fd;
  open_file->file = file;
  list_push_back (entries, &open_file->elem);
  return fd;
}

/* Close the file specified by fd in the current thread's list of open files. */
void
process_close_file (int fd)
{
  struct list *entries = &thread_current ()->open_files;
  for (struct list_elem *e = list_begin (entries); e != list_end (entries);
       e = list_next (e))
    {
      struct open_file *entry = list_entry (e, struct open_file, elem);
      if (entry->fd == fd)
        {
          list_remove (&entry->elem);
          file_close (entry->file);
          free (entry);
          return;
        }
    }
}