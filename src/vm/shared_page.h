#include "vm/page.h"

struct shared_page {
    struct hash_elem hash_elem;   
    void *upage;                  
    struct file *file;           
    off_t ofs;                   
    size_t read_bytes;           
};

static unsigned shared_page_hash(const struct hash_elem *e, void *aux UNUSED);
static bool shared_page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static bool is_shared_page(void *upage);
static void share_page(void *upage, struct file *file, off_t ofs, size_t read_bytes, bool writable)