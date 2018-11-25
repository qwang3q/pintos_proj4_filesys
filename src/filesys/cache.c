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
#include "filesys/cache.h"

static struct list cache_all_blocks;

struct cache_block *
get_new_cache_block(void) {
    struct cache_block * cache_block = malloc (sizeof (struct  cache_block));
    cache_block->in_use = false;
    cache_block->accessed = false;
    cache_block->dirty = false;
    return cache_block;
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

void 
cache_flush(void) {
    struct list_elem *head;
    struct cache_block *c_block;

    if(!list_empty(&cache_all_blocks)) {
        for (head = list_begin (&cache_all_blocks); head != NULL && head != list_end (&cache_all_blocks); head = list_next (head)) {
            c_block = list_entry (head, struct cache_block, elem);
            if(c_block->dirty == true) {
                cache_write_back(c_block);
                c_block->dirty = false;
            }
        }
    }
}

// Clock algorithm evicting cache
void
cache_evict(void) {
    if(list_empty(&cache_all_blocks))
        return;

    struct list_elem * head;
    struct cache_block *c_block;

    head = list_begin (&cache_all_blocks);

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
            if(head == list_end(&cache_all_blocks)) {
                list_empty(&cache_all_blocks);
            } else {
                list_remove(head);
            }
            break;
        }

        if(head == NULL || head == list_end (&cache_all_blocks)) {
            break;
        } 
        
        head = list_next (head);
    }
}

struct cache_block *
cache_get_block(block_sector_t d_sector) {
    struct list_elem *head;

    if(!list_empty(&cache_all_blocks)) {
        struct cache_block *c_block;

        for (head = list_begin (&cache_all_blocks); head != NULL && head != list_end (&cache_all_blocks); head = list_next (head)) {
            c_block = list_entry (head, struct cache_block, elem);
            if(c_block->disk_sector == d_sector) {
                return c_block;
            }
        }
    }

    // If come here then there is no free block available, evict, then cache item
    while(!list_empty(&cache_all_blocks) && list_size(&cache_all_blocks) >= CACHE_CAPACITY) {
        cache_evict();
    }

    struct cache_block * new_elem = get_new_cache_block();
    new_elem->disk_sector = d_sector;
    new_elem->accessed = true;
    list_push_back(&cache_all_blocks, &new_elem->elem);

    return new_elem;
}
