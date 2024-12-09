#include "vm/page.h"

struct frame
{
    void *kaddr;
    struct spt_entry *spte;
    struct thread *owner;
    struct list_elem lru;
};

static struct list lru_list;
static struct lock lru_list_lock;

struct frame *falloc(enum palloc_flags);
void ffree(void *);
//void free_frame_thread (struct thread *);
//void __free_frame(struct frame *);s


void lru_list_init(void);
//oid add_page_to_lru_list(struct page *);
//struct page *find_page_from_lru_list(void *);
//void del_page_from_lru_list (struct page *);

//struct page *get_victim (void);