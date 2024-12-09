#include "vm/page.h"

struct frame
{
    void *kaddr;
    struct spt_entry *spte;
    struct thread *owner;
    struct list_elem lru;
};

struct frame *falloc(enum palloc_flags);
void ffree(void *);
//void free_frame_thread (struct thread *);
//void __free_frame(struct frame *);

void lru_list_init(void);

//struct page *get_victim (void);
void* try_to_free_pages (enum palloc_flags flags);