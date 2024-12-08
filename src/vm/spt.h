#ifndef VM_SPT_H
#define VM_SPT_H

#include <hash.h>
#include "filesys/file.h"
#include "filesys/off_t.h"


struct spt_elem {
    void *upage;             // User virtual address
    void *kpage;             // Kernal virtual address

    enum page_status {       // Where is the page?
        PAGE_ZERO,           // Zero-initialized page
        PAGE_FRAME,           // FRAME page
        PAGE_SWAP,            // Page is in swap space
        PAGE_FILE             // Page is from file
    } status;

    bool in_memory;          // Is currently in memory?
    size_t swap_index;       // Swap index (if applicable)

    struct file *file;       // File source (if applicable)
    off_t offset;            // Offset in the file
    size_t read_bytes;       // Bytes to read from the file
    size_t zero_bytes;       // Bytes to zero-initialize
    bool writable;           // can you write to this page?

    struct hash_elem h_elem;   // Hash table element
};


void init_spt (struct hash *spt);
void free_spt (struct hash *spt);

void init_spte_zero (struct hash *spt, void *upage);
void init_spte_frame (struct hash *spt, void *upage, void *kpage);
struct spt_elem* init_spte_file (struct hash *, void *, struct file *, off_t, uint32_t, uint32_t, bool);





#endif