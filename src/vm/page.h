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
  struct hash_elem elem;
  void *uaddr;
  bool writable;
  off_t offset;
  struct file *file;
  enum page_type type;
  bool present;
  bool write_back;
  struct frame *frame;
  off_t length;
  size_t swap; // swap slot number
};

struct page_table
{
  struct hash spt;
  uint32_t *pd;
  struct lock lock;
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