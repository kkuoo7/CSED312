#include "vm/spt.h"
#include "threads/thread.h"
#include "vm/ft.h"
#include <string.h>
#include "threads/vaddr.h"


static hash_hash_func spte_hash;
static hash_less_func spte_less;

static unsigned
spte_hash (const struct hash_elem *he, void *aux UNUSED)
{
  struct spt_elem *spte = hash_entry(he, struct spt_elem, h_elem);
  return hash_bytes (&spte->upage, sizeof (spte->kpage));
}

static bool 
spte_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct spt_elem *spte_a = hash_entry (a, struct spt_elem, h_elem);
  struct spt_elem *spte_b = hash_entry (b, struct spt_elem, h_elem);
  return spte_a->upage < spte_b->upage;
}

static void
spte_destroy (struct hash_elem *he, void *aux UNUSED)
{
  struct spt_elem *spte = hash_entry (he, struct spt_elem, h_elem);
  free(spte);
}

struct spt_elem *
find_spte (struct hash *spt, void *upage)
{
  struct spt_elem spte;
  spte.upage = upage;
  struct hash_elem *he = hash_find (spt, &spte.h_elem);
  return he != NULL ? hash_entry (he, struct spt_elem, h_elem) : NULL;
}

void
init_spt (struct hash *spt)
{
  hash_init (spt, spte_hash, spte_less, NULL);
}

void
free_spt (struct hash *spt)
{
  hash_destroy (spt, spte_destroy);
}

void
init_spte_zero (struct hash *spt, void *upage)
{
  struct spt_elem *e;
  e = (struct spt_elem *) malloc (sizeof *e);
  
  e->upage = upage;
  e->kpage = NULL;
  
  e->status = PAGE_ZERO;
  
  e->in_memory = false; 
  e->swap_index = NULL;

  e->file = NULL;
  e->writable = true;

  hash_insert (spt, &e->h_elem);
}

void
init_spte_frame (struct hash *spt, void *upage, void *kpage)
{
  struct spt_elem *e;
  e = (struct spt_elem *) malloc (sizeof *e);
  
  e->upage = upage;
  e->kpage = kpage;
  
  e->status = PAGE_FRAME;

  e->in_memory = true; 
  e->swap_index = NULL;

  e->file = NULL;
  e->writable = true;
  
  hash_insert (spt, &e->h_elem);
}

struct spt_elem*
init_spte_file (struct hash *spt, void *_upage, struct file *_file, off_t ofs, uint32_t _read_bytes, uint32_t _zero_bytes, bool _writable)
{
  struct spt_elem *e;
  
  e = (struct spte *)malloc (sizeof *e);

  e->upage = _upage;
  e->kpage = NULL;
  
  e->status = PAGE_FILE;

  e->in_memory = false;
  e->swap_index = NULL;

  e->file = _file;
  e->offset = ofs;
  e->read_bytes = _read_bytes;
  e->zero_bytes = _zero_bytes;
  e->writable = _writable;
  
  hash_insert (spt, &e->h_elem);
  
  return e;
}



