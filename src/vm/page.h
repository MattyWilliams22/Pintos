#ifndef PAGE_H
#define PAGE_H

#include <hash.h>
#include "threads/malloc.h"
#include "filesys/file.h"

enum page_type
{
  FILE,
  FRAME,
  ZERO
};

/* Supplemental page table. (per process) */
struct page
{
  struct hash_elem elem;
  void *key; // user virtual address
  uint8_t *kernel_addr; // kernel virtual address
  bool writable;
  off_t offset;
  size_t read_bytes;
  size_t zero_bytes;
  struct file *file;
  enum page_type type;
};

bool compare_pages(const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED);
unsigned hash_page(const struct hash_elem *elem, void *aux UNUSED);
void init_pt(struct hash *page_table);
void remove_page(struct hash_elem *elem, void *aux);
void free_pt(struct hash *page_table);
struct page *search_pt(struct hash *page_table, void *user_addr);
bool insert_pt(struct hash *page_table, struct page *page);
struct page *remove_pt(struct hash *page_table, void *user_addr);
bool create_file_page(struct hash *spt, void *user_page, struct file *id, off_t offset, uint32_t read_bytes, uint32_t zero_bytes, bool writable, bool new_page);
bool create_frame_page(struct hash *spt, void *user_page, void *kernel_page);
bool create_zero_page(struct hash *spt, void *user_addr);
bool load_page(struct hash *page_table, void *user_page);

#endif