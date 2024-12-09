#include "vm/frame.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"

static struct list_elem *get_next_lru_clock (void);

struct list_elem *lru_clock;

void
lru_list_init (void) 
{
    list_init(&lru_list);
    lock_init(&lru_list_lock);
}


struct frame *
falloc(enum palloc_flags flags) {

    lock_acquire(&lru_list_lock);

    // Allocate frame from the user pool
    void *kpage = palloc_get_page(flags);
    
    if (kpage == NULL) {//free frame doesn’t exist
	        lock_release(&lru_list_lock);
	        return NULL;
	}


    // Create new frame structure
    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL) {
        palloc_free_page(kpage);  
        lock_release(&lru_list_lock);
        return NULL;
    }
		//load frame info
    f->kaddr = kpage;  
    f->spte = NULL;
    f->owner = thread_current();

    list_push_back(&lru_list, &f->lru);  // Add frame to frame_table

    lock_release(&lru_list_lock);

    return f;

}

void 
ffree(void *kpage) {
    ASSERT(kpage != NULL);

    lock_acquire(&lru_list_lock);

    // Search for the frame in the frame table
    struct list_elem *e;
    for (e = list_begin(&lru_list); e != list_end(&lru_list); e = list_next(e)) {
        struct frame *f = list_entry(e, struct frame, lru);
        if (f->kaddr == kpage) {
            // Remove the frame from the frame table
            list_remove(&f->lru);

            // Free the physical memory and the frame structure
            palloc_free_page(f->kaddr);
            free(f);

            lock_release(&lru_list_lock);
            return;
        }
    }

    lock_release(&lru_list_lock);
    // If no frame was found for the given kpage
    printf("ffree: The frame does not exsist.");
}

/*
void add_page_to_lru_list(struct page *page)
{

}

struct page *find_page_from_lru_list(void *kaddr)
{

}

void del_page_from_lru_list (struct page *page)
{

}

struct page * get_victim (void) 
{
    
}*/
