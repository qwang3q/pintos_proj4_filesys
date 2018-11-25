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
#include "cache.h"

static struct list cache_all_blocks;

struct cache_block *
get_new_cache_block(void) {
    struct cache_block * c_block = malloc(sizeof * cache_block);
    c_block->in_use = false;
    c_block->accessed = false;
    c_block->dirty = false;
    return c_block;
}

void
cache_init(void) {
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
cache_evict(void) {
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
            list_remove(head);
            break;
        }
        
        head = list_next(head);
    }
}

struct cache_block *
cache_get_block(block_sector_t * d_sector) {
    struct list_elem * head = list_front(&cache_all_blocks);
    struct cache_block *c_block;

    while(head != NULL) {
        c_block = list_entry (head, struct cache_block, elem);
        if(c_block->disk_sector == d_sector) {
            return c_block;
        }

        head = list_next(head);
    }

    // If come here then there is no free block available, evict, then cache item
    while(list_size(&cache_all_blocks) >= CACHE_CAPACITY) {
        cache_evict();
    }

    struct cache_block * new_elem = get_new_cache_block();
    new_elem->disk_sector = d_sector;
    list_push_back(&cache_all_blocks, &new_elem->elem);

    return new_elem;
}
