#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>

#include "vm/swap.h"
          
/* Block device that contains the swap */
struct block *swap_device;

/* Bitmap of swap slot availablities and corresponding lock */
static struct bitmap *swap_map;

/* Represents how many sectors are needed to store a page */
static size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;
static size_t swap_size_in_page (void);

void
vm_swap_init ()
{
  /* init the swap device */
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("no swap device found, can't initialize swap");

  /* init the swap bitmap */
  swap_map = bitmap_create (swap_size_in_page ());
  if (swap_map == NULL)
    PANIC ("swap bitmap creation failed");

  /* initialize all bits to be true */ 
  bitmap_set_all (swap_map, true);
}

/* Find an available swap slot and dump in the given page represented by UVA
   If failed, return SWAP_ERROR
   Otherwise, return the swap slot index */
/* No inner synchronization, should be used with a sync machanism */
size_t vm_swap_out (const void *uva)
{
  /* find a swap slot and mark it in use */
  size_t swap_idx = bitmap_scan_and_flip (swap_map, 0, 1, true);
    
  if (swap_idx == BITMAP_ERROR)
    return SWAP_ERROR;

  /* write the page of data to the swap slot */
  size_t counter = 0;
  while (counter < SECTORS_PER_PAGE)
    {
      block_write (swap_device, swap_idx * SECTORS_PER_PAGE + counter, 
		   uva + counter * BLOCK_SECTOR_SIZE);
      counter++;
    }
  return swap_idx;
}

/* Swap a page of data in swap slot SWAP_IDX to a page starting at UVA */
void
vm_swap_in (size_t swap_idx, void *uva)
{
  /* swap out the data from swap slot to mem page */
  size_t counter = 0;
  while (counter < SECTORS_PER_PAGE)
    {
      block_read (swap_device, swap_idx * SECTORS_PER_PAGE + counter,
		  uva + counter * BLOCK_SECTOR_SIZE);
      counter++;
    }
  /* free the corresponding swap slot bit in bitmap */
  bitmap_flip (swap_map, swap_idx);
}

void vm_clear_swap_slot (size_t swap_idx)
{
  /* free the corresponding swap slot bit in bitmap */
  bitmap_flip (swap_map, swap_idx);  
}

/* Returns how many pages the swap device can contain, which is rounded down */
static size_t
swap_size_in_page ()
{
  return block_size (swap_device) / SECTORS_PER_PAGE;
}

