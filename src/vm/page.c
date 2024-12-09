#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "lib/kernel/hash.h"

static unsigned spt_hash_func (const struct hash_elem *, void * UNUSED);
static bool spt_less_func (const struct hash_elem *, const struct hash_elem *, void * UNUSED);
static void spt_destroy_func (struct hash_elem *, void * UNUSED);


void spt_init (struct hash *spt)
{
    ASSERT(spt != NULL);
    hash_init(spt, spt_hash_func, spt_less_func, NULL);
}

void spt_destroy (struct hash *spt)
{
    ASSERT (spt != NULL);
    hash_destroy(spt, spt_destroy_func);
}

static unsigned
spt_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
    ASSERT (e != NULL);
    const struct spt_entry *spte = hash_entry(e, struct spt_entry, elem);
    return hash_int(spte->vaddr);
}

static bool 
spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    ASSERT (a != NULL);
    ASSERT (b != NULL);

    return hash_entry(a, struct spt_entry, elem)->vaddr 
        < hash_entry(b, struct spt_entry, elem)->vaddr;
}

static void 
spt_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
    ASSERT (e != NULL);
    struct spt_entry *spte = hash_entry(e, struct spt_entry, elem);

    if (spte->is_loaded && spte->kpage != NULL)
    {
        pagedir_clear_page(thread_current()->pagedir, spte->vaddr);
        palloc_free_page(spte->kpage);
    }

    // free_page_vaddr(spte->vaddr);
    // swap_clear(spte->swap_slot);
    free(spte);
}

struct spt_entry* find_spte(void *vaddr)
{
    struct hash *spt;
    struct spt_entry spte;
    struct hash_elem *elem;

    spt = &thread_current()->spt;
    spte.vaddr = pg_round_down(vaddr); // register hash-key (vaddr) into spte

    ASSERT (pg_ofs(spte.vaddr) == 0);

    elem = hash_find(spt, &spte.elem);

    if (elem)
        return hash_entry(elem, struct spt_entry, elem);
    else 
        return NULL;
}

bool insert_spte(struct hash *spt, struct spt_entry *spte)
{
    ASSERT(spt != NULL);
    ASSERT(spte != NULL);
    ASSERT (pg_ofs(spte->vaddr) == 0);

    struct hash_elem *elem;

    // Pintos doesn't allow duplicate key in supplemental page(hash) table
    if (hash_insert(spt, &spte->elem) == NULL)
        return true;
    else 
        return false;
}

bool delete_spte(struct hash *spt, struct spt_entry *spte)
{
    ASSERT(spt != NULL);
    ASSERT(spte != NULL);

    if (!hash_delete(spt, &spte->elem)) // if return null pointer
    {
        return false;
    }

    if (spte->is_loaded && spte->kpage != NULL)
    {
        pagedir_clear_page(thread_current()->pagedir, spte->vaddr);
        palloc_free_page(spte->kpage);
    }
    
    // free_page_vaddr(spte->vaddr);
    // swap_clear(spte->swap_slot);
    free(spte);

    return true;
}

bool load_file(void *kaddr, struct spt_entry *spte)
{
    ASSERT(kaddr != NULL);
    ASSERT(spte != NULL);
    ASSERT(spte->type == VM_BIN || spte->type == VM_FILE);


    // Read read_bytes size from 'file + offset' into kaddr
    if (file_read_at(spte->file, kaddr, spte->read_bytes, spte->offset) 
        != spte->read_bytes)
    {
        return false;
    }

    memset (kaddr + spte->read_bytes, 0, spte->zero_bytes);
    return true;
}

// static void collect (void)
// {

// }
/*
void free_page_vaddr(void *vaddr)
{
    free_page_kaddr(pagedir_get_page(thread_current()->pagedir, vaddr));
}

void free_page_kaddr(void *kaddr)
{
    lock_acquire(&lru_list_lock);

}

void __free_page (struct page *page)
{

}

*/


