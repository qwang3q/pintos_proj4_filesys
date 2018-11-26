#ifndef FILESYS_CACHE_EXECUTED
#define FILESYS_CACHE_EXECUTED

#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_CAPACITY 64

struct cache_block {
    uint8_t block[BLOCK_SECTOR_SIZE];
    block_sector_t disk_sector;

    bool free;
    int c_in_use;
    bool accessed;
    bool dirty;
};

struct lock cache_lock;

struct cache_block cache_all_blocks[CACHE_CAPACITY];

void new_cache_block(int i_block);
void cache_init(void);
void cache_maintenance_job(void *aux);
int cache_get_free_block(void);
void cache_write_back(int i_block);
void cache_flush(void);
// Clock algorithm evicting cache
void cache_evict(void); 
int cache_get_block(block_sector_t d_sector);

#endif
