#define CACHE_CAPACITY 64

#include "devices/block.h"
#include "lib/kernel/list.h"

struct cache_block {
    struct list_elem elem;

    uint8_t block[BLOCK_SECTOR_SIZE];
    block_sector_t * disk_sector;

    bool in_use;
    bool accessed;
    bool dirty;
};

struct cache_block * get_new_cache_block(void);
void cache_init(void);
void cache_mark_block_dirty(struct cache_block * c_block);
void  cache_write_back(struct cache_block * c_block);
// Clock algorithm evicting cache
void cache_evict(void);
struct cache_block * cache_get_block(block_sector_t * d_sector);
