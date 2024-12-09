#include "vm/swap.h"
#include <bitmap.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"

struct lock swap_lock;
struct bitmap *swap_bitmap;

void
swap_init (size_t size)
{

}

extern struct lock file_lock;

void swap_in (size_t used_index, void *kaddr)
{

}

void swap_clear (size_t used_index)
{

}

size_t swap_out (void *kaddr)
{

}