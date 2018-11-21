/*
Cache file blocks, 64 sectors in total
    - cache replacement as good as "clock" algorithm
    - remove "bounce buffer"
    - write-behind
    - read-ahead
*/

#include "devices/block.h"
#include "filesys/filesys.h"
#include "lib/kernel/list.h"

#define CACHE_CAPACITY 64

struct
cache_block {
    struct list_elem elem;

    struct block * block;
    block_sector_t disk_sector;

    bool in_use;
    bool accessed;
    bool free;
    bool dirty;
};

static struct list cache_all_blocks;

void
cache_init() {
    // Initialize cache
    list_init (&cache_all_blocks);
}

void
cache_mark_block_dirty(struct cache_block * c_block) {
    c_block->dirty = true;
}

void 
cache_write_back(struct cache_block * c_block) {
    if(!c_block->dirty)
        return;
    
    // TODO: write back to disk
    block_write(fs_device, c_block->disk_sector, c_block->block);
}

// Clock algorithm evicting cache
void
cache_evict() {
    struct list_elem * head = list_front(&cache_all_blocks);
    struct cache_block *c_block;

    while(head != NULL) {
        c_block = list_entry (head, struct cache_block, elem);
        if(c_block->in_use)
            continue;
        
        if(c_block->accessed) {
            // Give it second chance
            c_block->accessed = false;
        } else {
            // evict
            cache_write_back(c_block);
        }
        
        head = list_next(head);
    }
}

struct cache_block *
cache_get_free_block() {
    struct list_elem * head = list_front(&cache_all_blocks);
    struct cache_block *c_block;

    while(head != NULL) {
        c_block = list_entry (head, struct cache_block, elem);
        if(c_block->free) // Found and return
            return c_block;
        
        head = list_next(head);
    }

    // If come here then there is no free block available, evict
    cache_evict();
}


