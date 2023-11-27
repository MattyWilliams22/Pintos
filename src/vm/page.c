#include <string.h>
#include <debug.h>

#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"

/* Compare hash key of two pages. */
bool
compare_pages (const struct hash_elem *elem_a, const struct hash_elem *elem_b,
            void *aux UNUSED)
{
  const struct page *page_a = hash_entry (elem_a, struct page, elem);
  const struct page *page_b = hash_entry (elem_b, struct page, elem);
  return page_a->key < page_b->key;
}

/* Calculates hash from hash key. */
unsigned
hash_page (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct page *p = hash_entry (elem, struct page, elem);
  return hash_bytes (&p->key, sizeof(p->key));
}

/* Initializes supplemental page table. */
void
init_pt (struct hash *page_table)
{
  hash_init(page_table, hash_page, compare_pages, NULL);
}

void
remove_page (struct hash_elem *elem, void *aux UNUSED) 
{
  struct page *page = hash_entry(elem, struct page, elem);
  free(page); 
}

void
free_pt (struct hash *page_table)
{
  hash_destroy(page_table, remove_page);
  free(page_table);
}

struct page *
search_pt (struct hash *page_table, void *user_addr)
{
  struct page p;
  struct hash_elem *e;

  p.key = user_addr;
  e = hash_find (page_table, &p.elem);
  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

bool
insert_pt (struct hash *page_table, struct page *page)
{
  struct hash_elem *elem = hash_insert (page_table, &page->elem);
  return elem == NULL;
}

struct page *
remove_pt (struct hash *page_table, void *user_addr)
{
  struct page page;
  struct hash_elem *e;

  page.key = user_addr;
  e = hash_delete (page_table, &page.elem);
  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

bool
create_file_page (struct hash *page_table, void *user_page, struct file *id, off_t offset,
                   uint32_t read_bytes, uint32_t zero_bytes, bool writable,
                   bool new)
{
  struct page *page = new ? malloc (sizeof (struct page))
                          : search_pt (page_table, user_page);
  if (page == NULL)
  {
    return false;
  }

  page->key = user_page;
  page->kernel_addr = NULL;
  page->file = id;
  page->offset = offset;
  page->read_bytes = read_bytes;
  page->zero_bytes = zero_bytes;
  page->writable = writable;
  page->type = FILE;

  if (new)
    if (!insert_pt (page_table, page))
      {
        free (page);
        return false;
      }

  return true;
}

bool
create_frame_page (struct hash *page_table, void *user_page, void *kernel_page)
{
  struct page *page = malloc (sizeof (struct page));
  if (page == NULL)
  {
    return false;
  }

  page->key = user_page;
  page->kernel_addr = kernel_page;
  page->type = FRAME;
  page->writable = true;
  page->file = NULL;

  if (!insert_pt (page_table, page))
    {
      free (page);
      return false;
    }
  return true;
}

bool
create_zero_page (struct hash *page_table, void *user_addr)
{
  struct page *page = malloc (sizeof (struct page));
  if (page == NULL)
  {
    return false;
  }

  page->key = user_addr;
  page->kernel_addr = NULL;
  page->type = ZERO;
  page->writable = true;
  page->file = NULL;


  if (!insert_pt (page_table, page))
  {
    free (page);
    return false;
  }
  return true;
}

bool
load_page (struct hash *page_table, void *user_page)
{
  struct page *page = search_pt (page_table, user_page);
  if (page == NULL)
  {
    return false;
  }
  void *kernel_page = allocate_frame (PAL_USER, user_page);
  if (kernel_page == NULL)
  {
    return false;
  }

  switch (page->type)
    {
    case ZERO:
      memset (kernel_page, 0, PGSIZE);
      break;
    case FILE:
      if (file_read_at (page->file, kernel_page, page->read_bytes, page->offset) != (int) page->read_bytes)
      {
        free_frame (kernel_page);
        return false;
      }
      memset (kernel_page + page->read_bytes, 0, page->zero_bytes);
      break;
    case FRAME:
      break;
    default:
      PANIC ("Loading page of unknown type");
    }

  if (!pagedir_set_page (thread_current ()->pagedir, user_page, kernel_page, page->writable))
  {
    free_frame (kernel_page);
    return false;
  }
  page->kernel_addr = kernel_page;
  page->type = FRAME;
  return true;
}