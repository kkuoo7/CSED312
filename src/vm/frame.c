#include "vm/frame.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/vaddr.h"

static struct list_elem *get_next_lru_clock (void);

struct list lru_list;
struct lock lru_list_lock;
struct list_elem *lru_clock;

void
lru_list_init (void) 
{

}


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
    
}