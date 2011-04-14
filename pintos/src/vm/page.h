#ifndef VM_PAGE_H
#define VM_PAGE_H

#define STACK_SIZE (8 * (1 << 20))

#include <stdio.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"

/* *****************************************************
 * This file is about supplemental page table management
 *******************************************************/

/* Data Definition */

/* The lowest bit indicate whether the page has been swapped out */
enum suppl_pte_type
{
  SWAP = 001,
  FILE = 002,
  MMF  = 004
};

union suppl_pte_data
{
  struct
  {
    struct file * file;
    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
  } file_page;

  struct 
  {
    struct file *file;
    off_t ofs;
    uint32_t read_bytes;
  } mmf_page;
};

/* supplemental page table entry */
struct suppl_pte
{
  void *uvaddr;   //user virtual address as the unique identifier of a page
  enum suppl_pte_type type;
  union suppl_pte_data data;
  bool is_loaded;

  /* reserved for possible swapping */
  size_t swap_slot_idx;
  bool swap_writable;

  struct hash_elem elem;
};

/* Initialization of the supplemental page table management provided */
void vm_page_init(void);

/* Functionalities required by hash table, which is supplemental_pt */
unsigned suppl_pt_hash (const struct hash_elem *, void * UNUSED);
bool suppl_pt_less (const struct hash_elem *, 
		    const struct hash_elem *,
		    void * UNUSED);


/* insert the given suppl pte */
bool insert_suppl_pte (struct hash *, struct suppl_pte *);

/* Add a file supplemental page table entry to the current thread's
 * supplemental page table */
bool suppl_pt_insert_file ( struct file *, off_t, uint8_t *, 
			    uint32_t, uint32_t, bool);

/* Add a memory-mapped-file supplemental page table entry to the current
 * thread's supplemental page table */
bool suppl_pt_insert_mmf (struct file *, off_t, uint8_t *, uint32_t);

/* Given hash table and its key which is a user virtual address, find the
 * corresponding hash element*/
struct suppl_pte *get_suppl_pte (struct hash *, void *);

/* Given a suppl_pte struct spte, write data at address spte->uvaddr to
 * file. It is required if a page is dirty */
void write_page_back_to_file_wo_lock (struct suppl_pte *);

/* Free the given supplimental page table, which is a hash table */
void free_suppl_pt (struct hash *);

/* Load page data to the page defined in struct suppl_pte. */
bool load_page (struct suppl_pte *);

/* Grow stack by one page where the given address points to */
void grow_stack (void *);

#endif /* vm/page.h */
