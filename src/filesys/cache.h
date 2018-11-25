#define CACHE_CAPACITY 64

#include "devices/block.h"

struct cache_block {
    uint8_t block[BLOCK_SECTOR_SIZE];
    block_sector_t disk_sector;

    bool free;
    bool in_use;
    bool accessed;
    bool dirty;
};

struct cache_block cache_all_blocks[CACHE_CAPACITY];

void new_cache_block(int i_block);
void cache_init(void);
void cache_mark_block_dirty(struct cache_block * c_block);
int cache_get_free_block(void);
void cache_write_back(int i_block);
void cache_flush(void);
// Clock algorithm evicting cache
void cache_evict(void); 
struct cache_block cache_get_block(block_sector_t d_sector);
