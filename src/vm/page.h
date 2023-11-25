#include <hash.h>
#include "threads/synch.h"

struct spt
{
    struct hash page_table;
    struct lock lock;
    uint32_t *page_dir;
}

bool init_page_table(void);