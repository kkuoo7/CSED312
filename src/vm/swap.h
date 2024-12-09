#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/page.h"

void swap_init();
void swap_in(struct spt_entry *, void *);
void swap_clear(size_t);
size_t swap_out (void *);

#endif