#include "vm/spt.h"
#include "threads/thread.h"
#include "vm/ft.h"
#include <string.h>
#include "threads/vaddr.h"


static hash_hash_func spte_hash;
static hash_less_func spte_less;

static unsigned
spt_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct spt_elem *spte = hash_entry(e, struct spt_elem, elem);
  return hash_bytes (&spte->upage, sizeof (spte->kpage));
}

static bool 
spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct spt_elem *sptea = hash_entry (a, struct spt_elem, elem);
  struct spt_elem *spteb = hash_entry (b, struct spt_elem, elem);
  return sptea->upage < spteb->upage;
}

static void
spte_destroy (struct hash_elem *e, void *aux)
{
  struct spt_elem *spte = hash_entry (e, struct spt_elem, elem);
  free(e);
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
  
  e->in_memory = true; 
  e->swap_index = NULL;

  e->file = NULL;
  e->writable = true;

  hash_insert (spt, &e->elem);
}

void
init_spte_frame (struct hash *spt, void *upage, void *kpage)
{
  struct spt_elem *e;
  e = (struct spt_elem *) malloc (sizeof *e);
  
  e->upage = upage;
  e->kpage = kpage;
  
  e->status = PAGE_FRAME;
  
  hash_insert (spt, &e->elem);
}






