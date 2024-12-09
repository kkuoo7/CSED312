#include "vm/page.h"

void lru_list_init(void);
void add_page_to_lru_list(struct page *);
struct page *find_page_from_lru_list(void *);
void del_page_from_lru_list (struct page *);

struct page *get_victim (void);