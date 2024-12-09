#include "vm/ft.h"
#include "threads/synch.h"
#include "threads/vaddr.h"


void 
frame_table_init(void) {
    list_init(&frame_table);
    lock_init(&frame_table_lock);
}

struct frame *
falloc(void *upage, struct thread *owner) {

    lock_acquire(&frame_table_lock);

    // Allocate frame from the user pool
    void *kpage = palloc_get_page(PAL_USER);
    
    if (kpage == NULL) {//free frame doesn’t exist
		    evict_frame();//try evict
		    kpage = palloc_get_page(PAL_USER);//try again
        if (kpage == NULL) {//still nothing
	        lock_release(&frame_table_lock);
	        return NULL;
		    }
    return NULL;
    }


    // Create new frame structure
    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL) {
        palloc_free_page(kpage);  
        lock_release(&frame_table_lock);
        return NULL;
    }
		//load frame info
    f->kpage = kpage;  
    f->upage = upage;
    f->owner = owner;

    list_push_back(&frame_table, &f->elem);  // Add frame to frame_table

    lock_release(&frame_table_lock);
    ASSERT (pg_ofs (f->kpage) == 0);

    return f;

}

struct frame *
zfalloc(void *upage, struct thread *owner) {

    lock_acquire(&frame_table_lock);

    // Allocate frame from the user pool
    void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    
    if (kpage == NULL) {//free frame doesn’t exist
		    evict_frame();//try evict
		    kpage = palloc_get_page(PAL_USER | PAL_ZERO);//try again
        if (kpage == NULL) {//still nothing
	        lock_release(&frame_table_lock);
	        return NULL;
		    }
    return NULL;
    }


    // Create new frame structure
    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL) {
        palloc_free_page(kpage);  
        lock_release(&frame_table_lock);
        return NULL;
    }
		//load frame info
    f->kpage = kpage;  
    f->upage = upage;
    f->owner = owner;

    list_push_back(&frame_table, &f->elem);  // Add frame to frame_table

    lock_release(&frame_table_lock);
    return f;

}


void 
evict_frame(void) {
    //evict should allways be called in falloc
    if(!lock_held_by_current_thread(&frame_table_lock)) PANIC("evict_frame:sync error");
    //some eviction method
    return NULL;
}

void 
ffree(void *kpage) {
    ASSERT(kpage != NULL);

    lock_acquire(&frame_table_lock);

    // Search for the frame in the frame table
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame *f = list_entry(e, struct frame, elem);
        if (f->kpage == kpage) {
            // Remove the frame from the frame table
            list_remove(&f->elem);

            // Free the physical memory and the frame structure
            palloc_free_page(f->kpage);
            free(f);

            lock_release(&frame_table_lock);
            return;
        }
    }

    lock_release(&frame_table_lock);
    // If no frame was found for the given kpage
    printf("ffree: The frame does not exsist.");
}