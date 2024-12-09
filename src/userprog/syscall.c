#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

//added
// struct semaphore rw_mutex, mutex;
// int read_count;

#define USER_START_ADDRESS 0x08048000

struct spt_entry *check_address (void *addr)
{
  if (addr < (void *)USER_START_ADDRESS || addr >= (void *)0xc0000000)
  {
    // printf("\ncheck_address in syscall.c: %p\n", addr);
    sys_exit(-1);
  }

  struct spt_entry *spte = find_spte(addr);
  if (!spte)
    sys_exit(-1);
  
  return spte;
}

void check_valid_buffer (void *buffer, unsigned size, bool to_write)
{
/*   void *start = pg_round_down(buffer);
  void *end = buffer + size;

  for (void *addr = start; addr < end; addr += PGSIZE)
  {
    struct spt_entry *spte = check_address(addr);

    if (to_write && !spte->writable)
      sys_exit(-20);
  } */

 struct spt_entry *spte = check_address(buffer);
 
 if (to_write && !spte->writable)
 {
    sys_exit(-1);
 }

}

void check_valid_string(const void *str)
{
/*   const char *char_ptr = (const char *)str;

  while(true)
  {
    struct spt_entry *spte = check_address((void *)char_ptr);

    if (!spte)
      sys_exit(-30);

    if (*char_ptr == '\0')
      break;
    
    char_ptr++;
  } */

  check_address(str);

}


void 
get_argument (void *esp, int *arg, int count)
{
  for (int i = 0; i < count; i++)
  {
    int *ptr = (int *)esp + i + 1;  // esp를 int *로 캐스팅 후 포인터 연산 수행
    //printf("get_argument: esp[%d] = %p\n", i, (void *)ptr);

    check_address(ptr);
    arg[i] = *ptr;       // 값을 읽어 arg[i]에 저장

    //printf("get_argument: arg[%d] = %d (as pointer: %p)\n", i, arg[i], (void *)arg[i]);
  }
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  //printf("syscall_handler: esp = %p\n", f->esp);
  check_address(f->esp);

  int arg[3];
  int syscall_number = *(int *)(f->esp);

  //printf("syscall_handler: syscall_number = %d\n", syscall_number);

  switch (syscall_number)
  {
    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_EXIT:
      get_argument (f->esp, arg, 1);
      sys_exit (arg[0]);
      break;

    case SYS_EXEC:
      get_argument (f->esp, arg, 1);
      check_valid_string ((const char *) arg[0]);
      f->eax = sys_exec ((const char *) arg[0]);
      break;

    case SYS_WAIT:
      get_argument (f->esp, arg, 1);
      f->eax = sys_wait ((tid_t) arg[0]);
      break;

    case SYS_CREATE:
      get_argument (f->esp, arg, 2);      
      check_valid_string ((const char *) arg[0]);
      f->eax = sys_create ((const char *) arg[0], (unsigned int) arg[1]);
      break;

    case SYS_REMOVE:
      get_argument (f->esp, arg, 1);
      check_valid_string ((const char *) arg[0]);
      f->eax = sys_remove ((const char *) arg[0]);
      break;

    case SYS_OPEN:
      get_argument (f->esp, arg, 1);
      check_valid_string ((const char *) arg[0]);
      f->eax = sys_open ((const char *) arg[0]);
      break;

    case SYS_FILESIZE:
      get_argument (f->esp, arg, 1);
      f->eax = sys_filesize (arg[0]);
      break;
      
    case SYS_READ:
      get_argument (f->esp, arg, 3);
      check_valid_buffer(arg[1], arg[2], true);
      f->eax = sys_read (arg[0], (void *) arg[1], (unsigned int) arg[2]);
      break;

    case SYS_WRITE:
      get_argument (f->esp, arg, 3);
      check_valid_buffer(arg[1], arg[2], false);
      f->eax = sys_write ((int) arg[0], (const void *) arg[1], (unsigned int) arg[2]);
      break;

    case SYS_SEEK:
      get_argument (f->esp, arg, 2);
      sys_seek (arg[0], (unsigned int) arg[1]);
      break;

    case SYS_TELL:
      get_argument (f->esp, arg, 1);
      f->eax = sys_tell (arg[0]);
      break;

    case SYS_CLOSE:
      get_argument (f->esp, arg, 1);
      sys_close (arg[0]);
      break;

    case SYS_MMAP:
      get_argument(f->esp, arg, 2);
      // check_address(arg[1]);
      f->eax = sys_mmap((int) arg[0], (void *) arg[1]);
      break;

    case SYS_MUNMAP:
      get_argument(f->esp, arg, 1);
      sys_munmap((int) arg[0]);
      break;    
  }
}

void 
sys_exit (int status)
{
  struct thread *t = thread_current(); 
  t->pcb->exit_code = status;
  
  printf ("%s: exit(%d)\n", t->name, status); //process exit message
  thread_exit ();
}

tid_t 
sys_exec (const char *cmd_line)
{
  tid_t pid = process_execute (cmd_line);
  struct thread *child = get_child_process (pid);

  if (pid == -1 || !(child->pcb->is_loaded))
  {
    return -1;
  }

  return pid;
}

int 
sys_wait (tid_t pid)
{
  return process_wait (pid);
}

bool 
sys_create (const char *file, unsigned initial_size)
{
  bool retval = filesys_create (file, initial_size);
  return retval;
}

bool 
sys_remove (const char *file)
{
  return filesys_remove (file);
}

int 
sys_open (const char *file)
{
  lock_acquire(&filesys_lock);
  
  struct file *_file;
  struct thread *t = thread_current ();
  int fd = 0;

  _file = filesys_open (file);
  if (_file == NULL) 
  {
    lock_release(&filesys_lock);
    return -1;
  }
    
  fd = process_add_file (_file);
  if (fd == -1)
  {
    file_close(_file);
    lock_release(&filesys_lock);
    return -1;
  }

  // t->pcb->run_file = _file;
  // file_deny_write(_file);

  lock_release(&filesys_lock);
  return fd;
}

int 
sys_filesize (int fd)
{
  lock_acquire(&filesys_lock);
  struct file *file = process_get_file(fd);

  if (file == NULL)
  {
    lock_release(&filesys_lock);
    return -1;
  }

  lock_release(&filesys_lock);
  return file_length (file);
}

int 
sys_read (int fd, void *buffer, unsigned size)
{
  lock_acquire(&filesys_lock);
  int read_bytes = -1; 

  if (fd == 0) // fd가 0이면, 키보드 입력 
  {
    unsigned i; 
    for (i = 0; i < size; i++)
    {
      ((uint8_t *)buffer)[i] = input_getc();
    }
    read_bytes = size;
  }
  else 
  {
    struct file *f = process_get_file(fd);
    if (f == NULL)
    {
      lock_release(&filesys_lock);
      return -1; 
    }

    read_bytes = file_read(f, buffer, size);
  }

  lock_release(&filesys_lock);
  return read_bytes;
}

int 
sys_write (int fd, const void *buffer, unsigned size)
{
  lock_acquire(&filesys_lock);

  int written_bytes = -1;

  if (fd == 1)// fd가 1일 경우, 표준 출력으로 간주하여 화면에 출력
  {
    putbuf(buffer, size); // 버퍼의 내용을 화면에 출력
    written_bytes = size;
  }
  else 
  { 
    struct file *f = process_get_file(fd);
    if (f == NULL)
    {
      lock_release(&filesys_lock);
      return -1; 
    }
    written_bytes = file_write(f, buffer, size);
  }

  lock_release(&filesys_lock);
  return written_bytes;
}

void 
sys_seek (int fd, unsigned position)
{
  lock_acquire(&filesys_lock);

  struct file *f = process_get_file(fd);
  if (f != NULL)
    file_seek (f, position);

  lock_release(&filesys_lock);
}

unsigned 
sys_tell (int fd)
{
  lock_acquire(&filesys_lock);
  
  int f_pos = -1;
  struct file *f = process_get_file(fd);
  if (f == NULL)
  { 
    lock_release(&filesys_lock);
    return -1;
  }
  f_pos = file_tell(f);

  lock_release(&filesys_lock);

  return f_pos;
}

void 
sys_close (int fd)
{
  lock_acquire(&filesys_lock);

  struct file *f = process_get_file(fd);
  if (f == NULL)
  {
    lock_release(&filesys_lock);
    return;
  }
  process_close_file(fd);

  lock_release(&filesys_lock);
}

/*fd: 프로세스의 가상 주소공간에 매핑할 파일
addr: 매핑을 시작할 주소(page 단위 정렬)
성공 시 mapping id를 리턴, 실패 시 에러코드(-1) 리턴
요구페이징에 의해 파일 데이터를 메모리로 로드*/
int 
sys_mmap (int fd, void *addr)
{

  if (pg_ofs (addr) != 0 || !addr)
    return -1;
  if (is_user_vaddr (addr) == false)
    return -1;

  void * ptr;
  for(ptr = addr; ptr < addr + sys_filesize(fd); ptr += PGSIZE) 
  {
	  if(find_spte(ptr)) 
    {
      return -1;
    }
  }

  struct mmap_file *mmap_file;
  struct file *file, *file_copy;

  mmap_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));
  memset (mmap_file, 0, sizeof(struct mmap_file));
  list_init (&mmap_file->spte_list);
  
  file = process_get_file(fd);
  file_copy = file_reopen(file); 

  mmap_file->file = file_copy;
  mmap_file->mapid = thread_current()->next_mapid++;

  list_push_back(&thread_current()->mmap_list, &mmap_file->elem);

  int length = file_length(mmap_file->file);
  int offset = 0;

  struct spt_entry *spte; 
  while (length > 0)
  {
    spte = (struct spt_entry *) malloc(sizeof(struct spt_entry));

    spte->type = VM_FILE;
    spte->is_loaded = false;
    spte->writable = true;
    spte->vaddr = addr;
    spte->offset = offset; // offset은 파일 내부 데이터 참조를 위한 디스크 상에서의 상대적 주소 
    spte->read_bytes = (length > PGSIZE) ? PGSIZE : length;
    spte->zero_bytes = PGSIZE - spte->read_bytes;
    spte->file = mmap_file->file;

    list_push_back(&mmap_file->spte_list, &spte->mmap_elem);
    insert_spte(&thread_current()->spt, spte);

    length -= PGSIZE;
    addr += PGSIZE;
    offset += PGSIZE;
  }

  return mmap_file->mapid;
}


/* mmap_list내에서 mapping에 해당하는 mapid를 갖는 모든 vm_entry을
해제
인자로 넘겨진 mapping 값이 CLOSE_ALL인, 경우 모든 파일 매핑을 제거
매핑 제거 시 do_munmap()함수 호출 */
void 
sys_munmap(int mapping) // consider CLOSE_ALL 
{
  // mmap_list 순회 
    // mapid match 검사 
    // vm_entry 제거 
    // mmap_file 제거 
    // file_close 
  // process_exit 수정

  struct list *mmap_list = &thread_current()->mmap_list;
  struct mmap_file *mmf;
  struct list_elem *e;

  for (e = list_begin(mmap_list); e != list_end(mmap_list); e = list_next(mmap_list))
  {
    mmf = list_entry(e, struct mmap_file, elem);

    if (mmf->mapid == mapping)
    {
      e = list_remove(e);
      do_unmap(mmf);
      break;
    }
  }
}


