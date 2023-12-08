#ifndef PAGE_H
#define PAGE_H

#include <hash.h>
#include "filesys/file.h"
#include "threads/synch.h"

/* The stack is limited to 8MB. */
#define STACK_LIMIT (8 * 1024 * 1024)
#define MAX_FAULT 32

enum page_type
{
  FILE,
  SWAP,
  ZERO
};

/* One page for the supplemental page table. */
struct page
{
  struct hash_elem elem;      /* Element used to add page to page table. */
  void *uaddr;                /* User address of the page. */
  bool writable;              /* Flags if page can be written to. */
  off_t offset;               /* Offset of segment. */
  struct file *file;          /* File struct of file being paged. */
  enum page_type type;        /* Type of file being paged. */
  bool present;               /* Flags if page can be accessed. */
  bool write_back;            /* Flags if page can write back. */
  struct frame *frame;        /* Frame struct. */
  off_t length;               /* Length of segment. */
  size_t slot;                /* Swap slot. */
};

struct page_table
{
  struct hash spt;            /* Hash table used to store pages. */
  uint32_t *pd;               /* Page directory. */
  struct lock lock;           /* Lock used to control access to page table. */
};

bool init_pt (struct page_table *page_table);
void free_pt (struct page_table *page_table);

bool create_file_page (struct page_table *page_table, void *uaddr, struct file *id, off_t offset,
                       uint32_t length, bool writable, bool write_back);
bool create_zero_page (struct page_table *page_table, void *uaddr, bool writable);
bool delete_page (struct page_table *page_table, void *uaddr);
bool available_page (struct page_table *page_table, void *uaddr);
void activate_pt (struct page_table *page_table);
bool already_mapped (struct page_table *page_table, void *uaddr);
bool in_stack (void *uaddr);
bool load_page (struct page_table *pt, void *uaddr);

#endif
