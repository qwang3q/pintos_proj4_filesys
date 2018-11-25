/*
Cache file blocks, 64 sectors in total
    - cache replacement as good as "clock" algorithm
    - remove "bounce buffer"
    - write-behind
    - read-ahead
*/

#include "devices/block.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "filesys/cache.h"

void
new_cache_block(int i_block) {
    cache_all_blocks[i_block].free = true;
    cache_all_blocks[i_block].c_in_use = 0;
    cache_all_blocks[i_block].accessed = false;
    cache_all_blocks[i_block].dirty = false;
}

void
cache_init(void) {
  lock_init(&cache_lock);

  // Initialize cache
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    new_cache_block(i_block);
  }

  // Set up a maintenance thread cleaning up
  thread_create("cache_maintenance_job", 0, maintenance_job, NULL);
}

void
maintenance_job(void) {
  while(1)
  {
      timer_sleep(TIMER_FREQ);
      cache_flush();
  }
}

void
cache_mark_block_dirty(struct cache_block * c_block) {
    c_block->dirty = true;
}

int
cache_get_free_block(void) {
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    if(cache_all_blocks[i_block].free == true) {
      return i_block;
    }
  }

  return -1;
}

void 
cache_write_back(int i_block) {
    if(!cache_all_blocks[i_block].dirty)
        return;
    
    // TODO: write back to disk
    block_write(fs_device, cache_all_blocks[i_block].disk_sector, cache_all_blocks[i_block].block);
}

void 
cache_flush(void) {
  lock_acquire(&cache_lock);
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    if(cache_all_blocks[i_block].dirty == true) {
      cache_write_back(i_block);
      cache_all_blocks[i_block].dirty = false;
    }
  }

  lock_release(&cache_lock);
}

// Clock algorithm evicting cache
void
cache_evict(void) {
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    if(cache_all_blocks[i_block].c_in_use > 0)
        continue;
    
    if(cache_all_blocks[i_block].accessed) {
        // Give it second chance
        cache_all_blocks[i_block].accessed = false;
    } else {
        // evict
        cache_write_back(i_block);
        new_cache_block(i_block);
    }
  }
}

struct cache_block * cache_get_block(block_sector_t d_sector) {
  lock_acquire(&cache_lock);
  int i_target_block = -1;
  int i_block;
  for(i_block = 0; i_block < CACHE_CAPACITY; i_block++) {
    if(cache_all_blocks[i_block].disk_sector == d_sector) {
      i_target_block = i_block;
      break;
    }
  }

  if(i_target_block == -1) {
    // If come here the block is not in cache yet, obtain a free block
    i_target_block = cache_get_free_block();

    while(i_target_block == -1) {
      // If come here then there is no free block available, evict, then cache item
      cache_evict();
      i_target_block = cache_get_free_block();
    }

    // This is a free block, load it with data
    block_read(fs_device, cache_all_blocks[i_target_block].disk_sector, &cache_all_blocks[i_target_block]);
  }

  cache_all_blocks[i_target_block].disk_sector = d_sector;
  cache_all_blocks[i_target_block].free = false;
  cache_all_blocks[i_target_block].c_in_use++;
  cache_all_blocks[i_target_block].accessed = true;

  lock_release(&cache_lock);

  return &cache_all_blocks[i_target_block];;
}
