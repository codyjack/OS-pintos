#include "vm/page.h"
#include "threads/pte.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "string.h"
#include "userprog/syscall.h"
#include "vm/swap.h"

static bool load_page_file (struct suppl_pte *);
static bool load_page_swap (struct suppl_pte *);
static bool load_page_mmf (struct suppl_pte *);
static void free_suppl_pte (struct hash_elem *, void * UNUSED);

/* init the supplemental page table and neccessary data structure */
void 
vm_page_init (void)
{
  return;
}

/* Functionality required by hash table*/
unsigned
suppl_pt_hash (const struct hash_elem *he, void *aux UNUSED)
{
  const struct suppl_pte *vspte;
  vspte = hash_entry (he, struct suppl_pte, elem);
  return hash_bytes (&vspte->uvaddr, sizeof vspte->uvaddr);
}

/* Functionality required by hash table*/
bool
suppl_pt_less (const struct hash_elem *hea,
               const struct hash_elem *heb,
	       void *aux UNUSED)
{
  const struct suppl_pte *vsptea;
  const struct suppl_pte *vspteb;
 
  vsptea = hash_entry (hea, struct suppl_pte, elem);
  vspteb = hash_entry (heb, struct suppl_pte, elem);

  return (vsptea->uvaddr - vspteb->uvaddr) < 0;
}

/* Given hash table and its key which is a user virtual address, find the
 * corresponding hash element*/
struct suppl_pte *
get_suppl_pte (struct hash *ht, void *uvaddr)
{
  struct suppl_pte spte;
  struct hash_elem *e;

  spte.uvaddr = uvaddr;
  e = hash_find (ht, &spte.elem);
  return e != NULL ? hash_entry (e, struct suppl_pte, elem) : NULL;
}

/* Load page data to the page defined in struct suppl_pte. */
bool
load_page (struct suppl_pte *spte)
{
  bool success = false;
  switch (spte->type)
    {
    case FILE:
      success = load_page_file (spte);
      break;
    case MMF:
    case MMF | SWAP:
      success = load_page_mmf (spte);
      break;
    case FILE | SWAP:
    case SWAP:
      success = load_page_swap (spte);
      break;
    default:
      break;
    }
  return success;
}

/* Load page data to the page defined in struct suppl_pte from the given file
   in struct suppl_pte */
static bool
load_page_file (struct suppl_pte *spte)
{
  struct thread *cur = thread_current ();
  
  file_seek (spte->data.file_page.file, spte->data.file_page.ofs);

  /* Get a page of memory. */
  uint8_t *kpage = vm_allocate_frame (PAL_USER);
  if (kpage == NULL)
    return false;
  
  /* Load this page. */
  if (file_read (spte->data.file_page.file, kpage,
		 spte->data.file_page.read_bytes)
      
      != (int) spte->data.file_page.read_bytes)
    {
      vm_free_frame (kpage);
      return false; 
    }
  memset (kpage + spte->data.file_page.read_bytes, 0,
	  spte->data.file_page.zero_bytes);
  
  /* Add the page to the process's address space. */
  if (!pagedir_set_page (cur->pagedir, spte->uvaddr, kpage,
			 spte->data.file_page.writable))
    {
      vm_free_frame (kpage);
      return false; 
    }
  
  spte->is_loaded = true;
  return true;
}


/* Load a mmf page whose details are defined in struct suppl_pte */
static bool
load_page_mmf (struct suppl_pte *spte)
{
  struct thread *cur = thread_current ();

  file_seek (spte->data.mmf_page.file, spte->data.mmf_page.ofs);

  /* Get a page of memory. */
  uint8_t *kpage = vm_allocate_frame (PAL_USER);
  if (kpage == NULL)
    return false;

  /* Load this page. */
  if (file_read (spte->data.mmf_page.file, kpage,
		 spte->data.mmf_page.read_bytes)
      != (int) spte->data.mmf_page.read_bytes)
    {
      vm_free_frame (kpage);
      return false; 
    }
  memset (kpage + spte->data.mmf_page.read_bytes, 0,
	  PGSIZE - spte->data.mmf_page.read_bytes);

  /* Add the page to the process's address space. */
  if (!pagedir_set_page (cur->pagedir, spte->uvaddr, kpage, true)) 
    {
      vm_free_frame (kpage);
      return false; 
    }

  spte->is_loaded = true;
  if (spte->type & SWAP)
    spte->type = MMF;

  return true;
}

/* Load a zero page whose details are defined in struct suppl_pte */
static bool
load_page_swap (struct suppl_pte *spte)
{
  /* Get a page of memory. */
  uint8_t *kpage = vm_allocate_frame (PAL_USER);
  if (kpage == NULL)
    return false;
 
  /* Map the user page to given frame */
  if (!pagedir_set_page (thread_current ()->pagedir, spte->uvaddr, kpage, 
			 spte->swap_writable))
    {
      vm_free_frame (kpage);
      return false;
    }
 
  /* Swap data from disk into memory page */
  vm_swap_in (spte->swap_slot_idx, spte->uvaddr);

  if (spte->type == SWAP)
    {
      /* After swap in, remove the corresponding entry in suppl page table */
      hash_delete (&thread_current ()->suppl_page_table, &spte->elem);
    }
  if (spte->type == (FILE | SWAP))
    {
      spte->type = FILE;
      spte->is_loaded = true;
    }

  return true;
}

/* Free the given supplimental page table, which is a hash table */
void free_suppl_pt (struct hash *suppl_pt) 
{
  hash_destroy (suppl_pt, free_suppl_pte);
}

/* Free supplemental page entry represented by the given hash element in
   hash table */
static void
free_suppl_pte (struct hash_elem *e, void *aux UNUSED)
{
  struct suppl_pte *spte;
  spte = hash_entry (e, struct suppl_pte, elem);
  if (spte->type & SWAP)
    vm_clear_swap_slot (spte->swap_slot_idx);

  free (spte);
}

/* insert the given suppl pte */
bool 
insert_suppl_pte (struct hash *spt, struct suppl_pte *spte)
{
  struct hash_elem *result;

  if (spte == NULL)
    return false;
  
  result = hash_insert (spt, &spte->elem);
  if (result != NULL)
    return false;
  
  return true;
}


/* Add an file suplemental page entry to supplemental page table */
bool
suppl_pt_insert_file (struct file *file, off_t ofs, uint8_t *upage, 
		      uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct suppl_pte *spte; 
  struct hash_elem *result;
  struct thread *cur = thread_current ();

  spte = calloc (1, sizeof *spte);
  
  if (spte == NULL)
    return false;
  
  spte->uvaddr = upage;
  spte->type = FILE;
  spte->data.file_page.file = file;
  spte->data.file_page.ofs = ofs;
  spte->data.file_page.read_bytes = read_bytes;
  spte->data.file_page.zero_bytes = zero_bytes;
  spte->data.file_page.writable = writable;
  spte->is_loaded = false;
      
  result = hash_insert (&cur->suppl_page_table, &spte->elem);
  if (result != NULL)
    return false;

  return true;
}

/* Add an file suplemental page entry to supplemental page table */
bool
suppl_pt_insert_mmf (struct file *file, off_t ofs, uint8_t *upage, 
		      uint32_t read_bytes)
{
  struct suppl_pte *spte; 
  struct hash_elem *result;
  struct thread *cur = thread_current ();

  spte = calloc (1, sizeof *spte);
      
  if (spte == NULL)
    return false;
  
  spte->uvaddr = upage;
  spte->type = MMF;
  spte->data.mmf_page.file = file;
  spte->data.mmf_page.ofs = ofs;
  spte->data.mmf_page.read_bytes = read_bytes;
  spte->is_loaded = false;
      
  result = hash_insert (&cur->suppl_page_table, &spte->elem);
  if (result != NULL)
    return false;

  return true;
}

/* Given a suppl_pte struct spte, write data at address spte->uvaddr to
 * file. It is required if a page is dirty */
void write_page_back_to_file_wo_lock (struct suppl_pte *spte)
{
  if (spte->type == MMF)
    {
      file_seek (spte->data.mmf_page.file, spte->data.mmf_page.ofs);
      file_write (spte->data.mmf_page.file, 
                  spte->uvaddr,
                  spte->data.mmf_page.read_bytes);
    }
}


/* Grow stack by one page where the given address points to */
void grow_stack (void *uvaddr)
{
  void *spage;
  struct thread *t = thread_current ();
  spage = vm_allocate_frame (PAL_USER | PAL_ZERO);
  if (spage == NULL)
    return;
  else
    {
      /* Add the page to the process's address space. */
      if (!pagedir_set_page (t->pagedir, pg_round_down (uvaddr), spage, true))
	{
	  vm_free_frame (spage); 
	}
    }
}







