#include "vm/swap.h"
#include <bitmap.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"

struct lock swap_lock;
struct bitmap *swap_bitmap;

#define SECTOR_NUM (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block *swap_disk;

void
swap_init ()
{
    swap_disk = block_get_role(BLOCK_SWAP);
    swap_bitmap = bitmap_create(block_size(swap_disk) / SECTOR_NUM);
    bitmap_set_all(swap_bitmap, true);
    lock_init(&swap_lock);
}


void swap_in(struct spt_entry *spte, void *kaddr)
{   
    
    int i;
    int id = spte->swap_slot;
    printf("swap_in called\n");
    lock_acquire(&swap_lock);
    {
        if (id > bitmap_size(swap_bitmap) || id < 0)
        {
            sys_exit(-1);
        }

        if (bitmap_test(swap_bitmap, id) == true)
        {
            /* This swapping slot is empty. */
            sys_exit(-1);
        }

        bitmap_set(swap_bitmap, id, true);
    }

    lock_release(&swap_lock);

    for (i = 0; i < SECTOR_NUM; i++)
    {
        block_read(swap_disk, id * SECTOR_NUM + i, kaddr + (i * BLOCK_SECTOR_SIZE));
    }
}

size_t swap_out(void *kaddr)
{
    int i;
    int id;
    printf("swap_out called\n");
    lock_acquire(&swap_lock);
    {
        id = bitmap_scan_and_flip(swap_bitmap, 0, 1, true);
    }
    lock_release(&swap_lock);

    for (i = 0; i < SECTOR_NUM; ++i)
    {
        block_write(swap_disk, id * SECTOR_NUM + i, kaddr + (BLOCK_SECTOR_SIZE * i));
    }

    return id;
}



/*
extern struct lock file_lock;
void swap_clear (size_t used_index)
{

}

*/
