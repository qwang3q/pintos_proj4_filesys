#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "devices/block.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

struct indirect_block
{
  block_sector_t blocks[INDIRECT_BLOCK_COUNT];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  if (pos >= inode->data.length)
    return -1;
  
  uint32_t block_index = pos / BLOCK_SECTOR_SIZE;

  // Direct blocks
  if(block_index < DIRECT_BLOCK_COUNT) {
    return inode->data.direct_blocks[block_index];
  }

  block_index -= DIRECT_BLOCK_COUNT;

  // Indirect blocks
  if(block_index < INDIRECT_BLOCK_COUNT) {
    block_sector_t ind_blocks[INDIRECT_BLOCK_COUNT];
    block_read(fs_device, inode->data.indirect, &ind_blocks);
    return ind_blocks[block_index];
  }

  block_index -= INDIRECT_BLOCK_COUNT;

  // Double
  block_sector_t blocks_this_level[INDIRECT_BLOCK_COUNT];
  block_read(fs_device, inode->data.d_indirect, &blocks_this_level);

  block_sector_t blocks_next_level[INDIRECT_BLOCK_COUNT];
  block_read(fs_device, blocks_this_level[block_index / INDIRECT_BLOCK_COUNT], &blocks_next_level);

  return blocks_next_level[block_index % INDIRECT_BLOCK_COUNT];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = true;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  // Allocate disk_inode, this ensure that 
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      static char zeros[BLOCK_SECTOR_SIZE];

      block_write (fs_device, sector, disk_inode);

      size_t sectors_size_this_level; 
      uint32_t i;
      //1 write direct block
      sectors_size_this_level = DIRECT_BLOCK_COUNT;
      if(sectors < sectors_size_this_level)
        sectors_size_this_level = sectors;
      
      for(i=0; i<sectors_size_this_level; i++) {
        if (free_map_allocate (1, &disk_inode->direct_blocks[i])) 
        {
          block_write (fs_device, disk_inode->direct_blocks[i], zeros); 
        } else {
          success = false;
        }
      }

      sectors -= sectors_size_this_level;

      //2 write indirect block
      sectors_size_this_level = INDIRECT_BLOCK_COUNT;
      if(sectors < sectors_size_this_level)
        sectors_size_this_level = sectors;

      if(sectors_size_this_level>0) {
        struct indirect_block * ind_block;
        ind_block = calloc (1, sizeof *ind_block);

        if (free_map_allocate (1, &disk_inode->indirect)) { 
          for(i=0; i<sectors_size_this_level; i++) {
            if (free_map_allocate (1, &ind_block->blocks[i])) 
            {
              block_write (fs_device, ind_block->blocks[i], zeros); 
            } else {
              success = false;
            }
          }

          // Associate this new data block arrays with indirect pointers
          block_write(fs_device, disk_inode->indirect, &ind_block->blocks);
        }
      }

      sectors -= sectors_size_this_level;

      //3 write double indirect block
      if(sectors>0) {
        struct indirect_block * d_ind_block;
        d_ind_block = calloc (1, sizeof *d_ind_block);

        uint32_t i = 0, j;

        if (free_map_allocate (1, &disk_inode->d_indirect)) { 
          while(sectors>0) {
            sectors_size_this_level = DOUBLE_INDIRECT_BLOCK_COUNT;
            if(sectors < sectors_size_this_level)
              sectors_size_this_level = sectors;

            struct indirect_block * ind_block;
            for(j=0; j<sectors_size_this_level; j++) {
              ind_block = calloc (1, sizeof *ind_block);
              if (free_map_allocate (1, &ind_block->blocks[j])) 
              {
                block_write (fs_device, ind_block->blocks[j], zeros); 
              }
               else {
                success = false;
              }
            }

            block_write(fs_device, d_ind_block->blocks[i], ind_block->blocks);

            sectors -= sectors_size_this_level;
            i++;
          }

          // Associate this new data block arrays with indirect pointers
          block_write(fs_device, disk_inode->d_indirect, &d_ind_block->blocks);
        }
      }

      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
      {
        free_map_release (inode->sector, 1);

        struct inode_disk * disk_inode = &inode->data;
        size_t sectors = bytes_to_sectors (disk_inode->length);

        size_t sectors_size_this_level;
        uint32_t i;
        //1 free direct block
        sectors_size_this_level = DIRECT_BLOCK_COUNT;
        if(sectors < sectors_size_this_level)
          sectors_size_this_level = sectors;
        
        for(i=0; i<sectors_size_this_level; i++) {
          free_map_release(disk_inode->direct_blocks[i], 1);
        }

        sectors -= sectors_size_this_level;

        //2 free indirect block
        sectors_size_this_level = INDIRECT_BLOCK_COUNT;
        if(sectors < sectors_size_this_level)
          sectors_size_this_level = sectors;

        if(sectors_size_this_level>0) {
          block_sector_t blocks_this_level[INDIRECT_BLOCK_COUNT];
          block_read(fs_device, disk_inode->indirect, &blocks_this_level);
          
          for(i=0; i<sectors_size_this_level; i++) {
            free_map_release(blocks_this_level[i], 1); 
          }

          free_map_release(disk_inode->indirect, 1);

          sectors -= sectors_size_this_level;
        }

        //3 free double indirect block
        if(sectors>0) {
          block_sector_t blocks_this_level[INDIRECT_BLOCK_COUNT];
          block_read(fs_device, disk_inode->d_indirect, &blocks_this_level);

          block_sector_t blocks_next_level[INDIRECT_BLOCK_COUNT];
          uint32_t i = 0, j;
          
          while(sectors>0) {
            sectors_size_this_level = INDIRECT_BLOCK_COUNT;
            if(sectors < sectors_size_this_level)
              sectors_size_this_level = sectors;

            block_read(fs_device, blocks_this_level[i], &blocks_next_level);
            for(j=0; j<sectors_size_this_level; j++) {
              free_map_release (blocks_next_level[j], 1);
            }

            free_map_release( blocks_this_level[i], 1);

            sectors -= sectors_size_this_level;
            i++;
          }
        }

        free_map_release(disk_inode->d_indirect, 1);
      }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  // uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // CHANGE: Read from cache
      read_from_cache(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      
      printf("CD- sector idx is: %d\n", sector_idx);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  // free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  // uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // CHANGE: use block cache for writing
      write_to_cache(sector_idx, sector_ofs, buffer + bytes_written, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
