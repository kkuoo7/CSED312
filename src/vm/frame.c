#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"




static struct list lru_list;
static struct lock lru_list_lock;
static struct frame *lru_cursor;

void
lru_list_init (void) 
{
    list_init(&lru_list);
    lock_init(&lru_list_lock);
    lru_cursor = NULL;
}


struct frame *
falloc(enum palloc_flags flags) {

    // Allocate frame from the user pool
    void *kpage = palloc_get_page(flags);
    while (kpage == NULL) {//free frame doesn’t exist
        printf("page eviction called\n");
        try_to_free_pages(flags);//try evict
        kpage = palloc_get_page(flags);
	}

    // Create new frame structure
    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL) {
        palloc_free_page(kpage);
        return NULL;
    }
		//load frame info
    f->kaddr = kpage;  
    f->spte = NULL;
    f->owner = thread_current();

    lock_acquire(&lru_list_lock);

    list_push_back(&lru_list, &f->lru);  // Add frame to list

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

void* try_to_free_pages (enum palloc_flags flags){
lock_acquire(&lru_list_lock);
  if (lru_cursor == NULL) lru_cursor = list_entry(list_begin(&lru_list), struct frame, lru);

  /*Find page to evict with clock */
  while (1){
    if(pagedir_is_accessed(lru_cursor->owner->pagedir, lru_cursor->spte->vaddr)) 
    pagedir_set_accessed(lru_cursor->owner->pagedir, lru_cursor->spte->vaddr, false);
    else break;

    if (list_next(&lru_cursor) == list_end(&lru_list)) {
      lru_cursor = list_entry(list_begin(&lru_list), struct frame, lru);
    } else {
      lru_cursor = list_next (lru_cursor);
    }
  }

  struct frame * victim = lru_cursor;
  if(victim->spte->type == VM_ANON || victim->spte->type == VM_BIN)
  {
  victim->spte->type = VM_ANON;
  victim->spte->swap_slot = swap_out(victim->kaddr);
  }
  if(victim->spte->type == VM_FILE) //자체 메커니즘 있다고 함;

  lock_release(&lru_list_lock);

    ffree(victim->kaddr);

}