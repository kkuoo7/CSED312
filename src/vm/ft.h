#ifndef VM_FT_H
#define VM_FT_H

#include <list.h>
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/malloc.h"


struct frame {
    void *kpage;             // Memory frame adress
    void *upage;             // User page mapped to this frame
    struct thread *owner;    // Thread owning this frame
    struct list_elem elem;   // Element for list
};

static struct list frame_table;  // Global list of frames
static struct lock frame_table_lock;  // Synchronize access to frame table

void frame_table_init(void);
struct frame *falloc(void *upage, struct thread *owner);
struct frame *zfalloc(void *upage, struct thread *owner);
void evict_frame(void);
void ffree(void *kpage);

#endif