/*
Cache file blocks, 64 sectors in total
    - cache replacement as good as "clock" algorithm
    - remove "bounce buffer"
    - write-behind
    - read-ahead
*/

#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/cache.h"

void
new_cache_block(int i_block) {
    struct cache_block c_block = cache_all_blocks[i_block];
    c_block.free = true;
    c_block.in_use = false;
    c_block.accessed = false;
    c_block.dirty = false;
}

void
cache_init(void) {
  // Initialize cache
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    new_cache_block(i_block);
  }
}

void
cache_mark_block_dirty(struct cache_block * c_block) {
    c_block->dirty = true;
}

int
cache_get_free_block(void) {
  struct cache_block c_block;
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    c_block = cache_all_blocks[i_block];
    if(c_block.free == true) {
      return i_block;
    }
  }

  return -1;
}

void 
cache_write_back(struct cache_block c_block) {
    if(!c_block.dirty)
        return;
    
    // TODO: write back to disk
    block_write(fs_device, c_block.disk_sector, c_block.block);
}

void 
cache_flush(void) {
  struct cache_block c_block;
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    c_block = cache_all_blocks[i_block];
    if(c_block.dirty == true) {
      cache_write_back(c_block);
      c_block.dirty = false;
    }
  }
}

// Clock algorithm evicting cache
void
cache_evict(void) {
  struct cache_block c_block;
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    c_block = cache_all_blocks[i_block];

    if(c_block.in_use)
        continue;
    
    if(c_block.accessed) {
        // Give it second chance
        c_block.accessed = false;
    } else {
        // evict
        cache_write_back(c_block);
        new_cache_block(i_block);
    }
  }
}

struct cache_block cache_get_block(block_sector_t d_sector) {
  struct cache_block c_block;
  int i_target_block = -1;
  bool found = false;

  while(i_target_block == -1) {
    int i_block;
    for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
      c_block = cache_all_blocks[i_block];

      if(c_block.disk_sector == d_sector) {
        i_target_block = i_block;
        break;
      }
    }

    if(i_target_block == -1) {
      // If come here then there is no free block available, evict, then cache item
      cache_evict();
      i_target_block = cache_get_free_block();
    }
  }

  struct cache_block target_c_block = cache_all_blocks[i_target_block];
  target_c_block.disk_sector = d_sector;
  target_c_block.free = false;
  target_c_block.accessed = true;

  return target_c_block;
}
