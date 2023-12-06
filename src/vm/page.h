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

/* Supplemental page table. (per process) */
struct page
{
  struct hash_elem elem;
  void *key;
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

bool create_file_page (struct page_table *page_table, void *user_page, struct file *id, off_t offset,
                       uint32_t length, bool writable, bool write_back);
bool create_zero_page (struct page_table *page_table, void *user_addr, bool writable);
bool delete_page (struct page_table *page_table, void *user_addr);
bool available_page (struct page_table *page_table, void *user_page);
void activate_pt (struct page_table *page_table);
bool already_mapped (struct page_table *page_table, void *user_page);
bool in_stack (void *user_page);
bool make_present (struct page_table *pt, void *user_page);

#endif