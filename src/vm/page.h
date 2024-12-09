#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h> 
#include <hash.h> 
#include "threads/palloc.h"

extern struct lock lru_list_lock;

enum vm_type
{
    VM_ANON,
    VM_FILE,
    VM_BIN
};

struct page
{
    void *kaddr;
    struct spt_entry *spte;
    struct thread *thread;
    struct list_elem lru;
};

struct mmap_file
{
    int mapid;
    struct file *file;
    struct list_elem elem;
    struct list spte_list;
};

struct spt_entry
{
    uint8_t type;
    void *vaddr;
    void *kpage;
    bool writable;
    bool is_loaded; 
    bool pinned; 

    struct file* file;
    struct list_elem mmap_elem;
    
    size_t offset;
    size_t read_bytes;
    size_t zero_bytes;
    size_t swap_slot;

    struct hash_elem elem; // linked to hash table (spt)
};

void spt_init(struct hash *);
void spt_destroy(struct hash *);

struct spt_entry* find_spte(void *vaddr);
bool insert_spte(struct hash *, struct spt_entry *);
bool delete_spte(struct hash *, struct spt_entry *);

bool load_file (void *kaddr, struct spt_entry *);

struct page *alloc_page(enum palloc_flags);
void free_page(void *);
void free_page_thread (struct thread *);
void __free_page(struct page *);

#endif /* VM_PAGE_H */