#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"

void init_entry(int idx)
{
  cache_array[idx].is_free = true;
  cache_array[idx].open_cnt = 0;
  cache_array[idx].dirty = false;
  cache_array[idx].accessed = false;
}

void init_cache(void)
{
  int i;
  lock_init(&cache_lock);
  for(i = 0; i < CACHE_MAX_SIZE; i++)
    init_entry(i);

  thread_create("cache_writeback", 0, func_periodic_writer, NULL);
}

int get_cache_entry(block_sector_t disk_sector)
{
  int i;
  for(i = 0; i < CACHE_MAX_SIZE; i++) {
    if(cache_array[i].disk_sector == disk_sector)
    {
      if(!cache_array[i].is_free)
      {
        return i;
      }
    }
  }
    
  return -1;
}

int get_free_entry(void)
{
  int i;
  for(i = 0; i < CACHE_MAX_SIZE; i++)
  {
    if(cache_array[i].is_free == true)
    {
      cache_array[i].is_free = false;
      return i;
    }
  }

  return -1;
}

int access_cache_entry(block_sector_t disk_sector, bool dirty)
{
  lock_acquire(&cache_lock);
  
  int idx = get_cache_entry(disk_sector);
  if(idx == -1)
    idx = replace_cache_entry(disk_sector, dirty);
  else
  {
    cache_array[idx].open_cnt++;
    cache_array[idx].accessed = true;
    cache_array[idx].dirty |= dirty;
  }

  lock_release(&cache_lock);
  return idx;
}

int replace_cache_entry(block_sector_t disk_sector, bool dirty)
{
  int idx = get_free_entry();
  int i = 0;
  if(idx == -1) //cache is full
  {
    for(i = 0; ; i = (i + 1) % CACHE_MAX_SIZE)
    {
          //cache is in use
      if(cache_array[i].open_cnt > 0)
        continue;

          //second chance
      if(cache_array[i].accessed == true)
        cache_array[i].accessed = false;

      //evict it
      else
      {
        //write back
        if(cache_array[i].dirty == true)
          block_write(fs_device, cache_array[i].disk_sector,
            &cache_array[i].block);

        init_entry(i);
        idx = i;
        break;
      }
    }
  }

  cache_array[idx].disk_sector = disk_sector;
  cache_array[idx].is_free = false;
  cache_array[idx].open_cnt++;
  cache_array[idx].accessed = true;
  cache_array[idx].dirty = dirty;
  block_read(fs_device, cache_array[idx].disk_sector, &cache_array[idx].block);

  return idx;
}

void func_periodic_writer(void *aux UNUSED)
{
    while(true)
    {
        timer_sleep(4 * TIMER_FREQ);
        write_back(false);
    }
}

void write_back(bool clear)
{
    int i;
    lock_acquire(&cache_lock);

    for(i = 0; i < CACHE_MAX_SIZE; i++)
    {
        if(cache_array[i].dirty == true)
        {
            block_write(fs_device, cache_array[i].disk_sector, &cache_array[i].block);
            cache_array[i].dirty = false;
        }

        // clear cache line (filesys done)
        if(clear) {
          init_entry(i);
        }
    }

    lock_release(&cache_lock);
}

void func_read_ahead(void *aux)
{
    block_sector_t disk_sector = *(block_sector_t *)aux;
    lock_acquire(&cache_lock);

    int idx = get_cache_entry(disk_sector);

    // need eviction
    if (idx == -1)
        replace_cache_entry(disk_sector, false);
    
    lock_release(&cache_lock);
    free(aux);
}

void ahead_reader(block_sector_t disk_sector)
{
    block_sector_t *arg = malloc(sizeof(block_sector_t));
    *arg = disk_sector + 1;  // next block
    thread_create("cache_read_ahead", 0, func_read_ahead, arg);
}




/*
Cache file blocks, 64 sectors in total
    - cache replacement as good as "clock" algorithm
    - remove "bounce buffer"
    - write-behind
    - read-ahead
*/

// #include "devices/block.h"
// #include "filesys/filesys.h"
// #include "lib/kernel/list.h"
// #include "filesys/cache.h"

// static struct list cache_all_blocks;

// struct cache_block *
// get_new_cache_block(void) {
//     struct cache_block * cache_block = malloc (sizeof (struct  cache_block));
//     cache_block->in_use = false;
//     cache_block->accessed = false;
//     cache_block->dirty = false;
//     return cache_block;
// }

// void
// cache_init(void) {
//     // Initialize cache
//     list_init (&cache_all_blocks);
// }

// void
// cache_mark_block_dirty(struct cache_block * c_block) {
//     c_block->dirty = true;
// }

// void 
// cache_write_back(struct cache_block * c_block) {
//     if(!c_block->dirty)
//         return;
    
//     // TODO: write back to disk
//     block_write(fs_device, c_block->disk_sector, c_block->block);
// }

// void 
// cache_flush(void) {
//     struct list_elem *head;
//     struct cache_block *c_block;

//     if(!list_empty(&cache_all_blocks)) {
//         for (head = list_begin (&cache_all_blocks); head != NULL && head != list_end (&cache_all_blocks); head = list_next (head)) {
//             c_block = list_entry (head, struct cache_block, elem);
//             if(c_block->dirty == true) {
//                 cache_write_back(c_block);
//                 c_block->dirty = false;
//             }
//         }
//     }
// }

// // Clock algorithm evicting cache
// void
// cache_evict(void) {
//     if(list_empty(&cache_all_blocks))
//         return;

//     struct list_elem * head;
//     struct cache_block *c_block;

//     head = list_begin (&cache_all_blocks);

//     while(head != NULL) {
//         c_block = list_entry (head, struct cache_block, elem);
//         if(c_block->in_use)
//             continue;
        
//         if(c_block->accessed) {
//             // Give it second chance
//             c_block->accessed = false;
//         } else {
//             // evict
//             cache_write_back(c_block);
//             if(head == list_end(&cache_all_blocks)) {
//                 list_empty(&cache_all_blocks);
//             } else {
//                 list_remove(head);
//             }
//             break;
//         }

//         if(head == NULL || head == list_end (&cache_all_blocks)) {
//             break;
//         } 
        
//         head = list_next (head);
//     }
// }

// struct cache_block *
// cache_get_block(block_sector_t d_sector) {
//     struct list_elem *head;

//     if(!list_empty(&cache_all_blocks)) {
//         struct cache_block *c_block;

//         for (head = list_begin (&cache_all_blocks); head != NULL && head != list_end (&cache_all_blocks); head = list_next (head)) {
//             c_block = list_entry (head, struct cache_block, elem);
//             if(c_block->disk_sector == d_sector) {
//                 return c_block;
//             }
//         }
//     }

//     // If come here then there is no free block available, evict, then cache item
//     while(!list_empty(&cache_all_blocks) && list_size(&cache_all_blocks) >= CACHE_CAPACITY) {
//         cache_evict();
//     }

//     struct cache_block * new_elem = get_new_cache_block();
//     new_elem->disk_sector = d_sector;
//     new_elem->accessed = true;
//     list_push_back(&cache_all_blocks, &new_elem->elem);

//     return new_elem;
// }
