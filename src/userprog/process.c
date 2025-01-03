#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "syscall.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

#define FD_MAX 64

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

//added functions
void
pars_filename (char *cmd)
{
  char *save_ptr;
  cmd = strtok_r (cmd, " ", &save_ptr);
}
int
pars_arguments (char *cmd, char **argv)
{
  char *token, *save_ptr;

  int argc = 0;

  for (token = strtok_r (cmd, " ", &save_ptr); token != NULL;
  token = strtok_r (NULL, " ", &save_ptr), argc++)
  {
    argv[argc] = token;
  }

  return argc;
}

void argument_stack (char **argv, int argc, struct intr_frame *if_)
{
  // 1. 인자를 스택에 역순으로 저장
  for (int i = argc - 1; i >= 0; i--)
  {
    size_t len = strlen (argv[i]) + 1;
    if_->esp -= len;
    memcpy(if_->esp, argv[i], len);
    argv[i] = (char *) if_->esp;
  }

  // 2. 스택을 4바이트로 정렬
  while ((uintptr_t) if_->esp % 4 != 0)
  {
    if_->esp -= 1;
    *((uint8_t *) if_->esp) = 0;
  }

  // 3. NULL 포인터 삽입
  if_->esp -= sizeof(char *);
  *(char **)(if_->esp) = 0;
  
  // 4. argv 배열의 포인터를 역순으로 스택에 저장
  for (int i = argc - 1; i >= 0; i--) 
  {
      if_->esp -= sizeof(char *);
      *(char **)(if_->esp) = argv[i];
  }

  // 5. argv의 시작 주소를 스택에 저장
  char **argv_start = if_->esp;
  if_->esp -= sizeof(char **);
  *(char ***)(if_->esp) = argv_start;
  
  // 6. argc 값을 스택에 저장
  if_->esp -= sizeof(int);
  *(uint32_t *)(if_->esp) = argc;

   // 7. NULL 리턴 주소를 위한 공간 할당
  if_->esp -= sizeof(void *);
  *(uint32_t *)(if_->esp) = 0;

  return;
}


void
init_stack_arg (char **argv, int argc, void **esp)
{
  /* Push ARGV[i][...] */
  int argv_len, i, len;
  for (i = argc - 1, argv_len = 0; i >= 0; i--)
  {
    len = strlen (argv[i]);
    *esp -= len + 1;
    argv_len += len + 1;
    strlcpy (*esp, argv[i], len + 1);
    argv[i] = *esp;
  }

  /* Align stack. */
  if (argv_len % 4)
    *esp -= 4 - (argv_len % 4);

  /* Push null. */
  *esp -= 4;
  **(uint32_t **)esp = 0;

  /* Push ARGV[i]. */
  for(i = argc - 1; i >= 0; i--)
  {
    *esp -= 4;
    **(uint32_t **)esp = argv[i];
  }

  /* Push ARGV. */
  *esp -= 4;
  **(uint32_t **)esp = *esp + 4;

  /* Push ARGC. */
  *esp -= 4;
  **(uint32_t **)esp = argc;

  /* Push return address. */
  *esp -= 4;
  **(uint32_t **)esp = 0;

 /*  printf("setup_stack: argc = %d\n", argc);
  for (int i = 0; i < argc; i++) {
      printf("setup_stack: argv[%d] = %s\n", i, argv[i]);
  }
  printf("setup_stack: argv[%d] = %p (NULL)\n", argc, argv[argc]); */

}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy, *file_name_parsed;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  file_name_parsed = palloc_get_page (0);
  if (file_name_parsed == NULL) {
    return TID_ERROR;
  }

  strlcpy (file_name_parsed, file_name, PGSIZE);
  parse_filename (file_name_parsed);

  //printf("\n\nprocess_execute: file_name_parsed: %s\n\n", file_name_parsed);

  tid = thread_create (file_name_parsed, PRI_DEFAULT, start_process, fn_copy);
  
  if (tid == TID_ERROR) {
    palloc_free_page (fn_copy); 
  } else {
    sema_down (&(get_child_process(tid)->pcb->sema_load));
  }
  
  palloc_free_page (file_name_parsed);

  return tid;
}

#ifdef USERPROG

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  spt_init(&thread_current()->spt);
  thread_current()->next_mapid = 0;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* parse arguments */
  char **argv = palloc_get_page(0);
  int argc = parse_arguments(file_name, argv);

  //printf("\n\nstart_process argc: %d, argv[0]: %s\n\n", argc, argv[0]);

  thread_current()->pcb->is_loaded = success = load (argv[0], &if_.eip, &if_.esp);

  /* If load failed, quit. */
  if (success)
  {
    //printf("Success to load %s\n", argv[0]);  // 디버깅을 위한 출력
    //argument_stack(argv, argc, &if_);
    init_stack_arg (argv, argc, &if_.esp);
    // hex_dump(if_.esp , if_.esp , PHYS_BASE - if_.esp ,true);
  }

  palloc_free_page (argv);
  palloc_free_page (file_name);
  
  sema_up (&(thread_current ()->pcb->sema_load));

  if (!success) 
    sys_exit (-1);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current();
  int exit_code;
  bool ret_flag = false;

  struct thread *child = get_child_process(child_tid);
  if (child == NULL || !child->pcb->is_loaded)
    return -1;

  //printf("process_wait start!\n\n");
  child->pcb->waited += 1;

  //printf("process_wait sleep!\n\n");
  sema_down (&(child->pcb->sema_wait)); // sema down to wait on child process
  //printf("process_wait wakeup!\n\n");
  
  exit_code = child->pcb->exit_code; // child가 종료됨을 알림

  if (!child->pcb->is_exited || child->parent_process != cur || child->pcb->waited != 1)
    return -1;

  // remove_child_process(child_tid); 
  list_remove(&child->child_elem);
  palloc_free_page(child->pcb);
  child->pcb = NULL;
  palloc_free_page (child);

  return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  struct thread *child;
  uint32_t *pd;
  struct list_elem *e; 
  struct list *mmap_list; 

  mmap_list = &thread_current()->mmap_list;
  for (e = list_begin(mmap_list); e != list_end(mmap_list);)
  {
    struct mmap_file *file = list_entry(e, struct mmap_file, elem);
    e = list_remove(e);
    do_unmap(file);

  }
  
  for (int fd = 2; fd <= FD_MAX; fd++)
  {
    process_close_file(fd);
  }

  // 파일 디스크립터 테이블 메모리 해제 후 NULL로 설정
  if (cur->pcb->fd_table != NULL) {
      palloc_free_page(cur->pcb->fd_table);
      cur->pcb->fd_table = NULL;
  }

  file_close(cur->pcb->run_file);

  spt_destroy(&cur->spt);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  cur->pcb->is_exited = true; // child process is about to exit
  sema_up (&(cur->pcb->sema_wait));  // sema up to notifiy parent process

  /* 만약 부모가 먼저 종료되었으면 실행 정보를 해제*/
    if (cur->parent_process == NULL && cur != NULL) 
    {
        palloc_free_page(cur->pcb);
        cur->pcb = NULL;
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* 자식 프로세스 디스크립터를 검색하는 함수 (get_child_process)
현재 프로세스의 자식 리스트를 검색하여 해당 pid에 맞는 프로세스 디스크립터를 반환
struct thread *thread_current(void) : 현재 프로세스의 디스크립터 반환
pid를 갖는 프로세스 디스크립터가 존재하지 않을 경우 NULL 반환 */
struct thread* 
get_child_process(tid_t child_tid)
{
  struct thread *t = thread_current();
  struct thread *child;
  struct list *child_list = &(t->children);

  for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e))
  {
    child = list_entry(e, struct thread, child_elem);

    if (child->tid == child_tid)
      return child;
  }

  return NULL;
}

/* 프로세스 디스크립터를 자식 리스트에서 제거 후 메모리 해제 */
void 
remove_child_process(tid_t child_tid)
{
  struct thread *t = thread_current();
  struct thread *child; 
  struct list *child_list = &(t->children);

  for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e))
  {
    child = list_entry(e, struct thread, child_elem);
    if (child->tid == child_tid)
    {
      list_remove(e);
      palloc_free_page(child->pcb);
      child->pcb = NULL;
      palloc_free_page (child);
      return;
    }
  }
}

int
parse_arguments (char *cmd, char **argv)
{
  char *token, *save_ptr;
  int argc = 0;

  for (token = strtok_r (cmd, " ", &save_ptr); token != NULL; 
  token = strtok_r (NULL, " ", &save_ptr), argc++)
  {
    argv[argc] = token;
  }

  return argc;
}

void
parse_filename (char *cmd)
{
  char *save_ptr;
  cmd = strtok_r (cmd, " ", &save_ptr);
  return;
}

/*파일 객체에 대한 파일 디스크립터 생성*/
int 
process_add_file(struct file *f) 
{
    struct thread *cur = thread_current();
    
    for (int fd = 2; fd <= FD_MAX; fd++)
    {
      if (cur->pcb->fd_table[fd] == NULL)
      {
        cur->pcb->fd_table[fd] = f;
        cur->pcb->next_fd = fd + 1;
        return fd;
      }
    }
    
    return -1;  // 파일 디스크립터 테이블이 꽉 찬 경우
}

/*프로세스의 파일 디스크립터 테이블을 검색하여 파일 객체의 주소를 리턴*/
struct file*
process_get_file (int fd)
{
  struct thread *cur = thread_current();

  if (fd >= 2 && fd <= FD_MAX)
    return cur->pcb->fd_table[fd];

  return NULL;

}

/*file_close() 를 호출하여, 파일 디스크립터에 해당하는 파일의 inode reference
count를 1씩 감소. 해당 파일 디스크립터 엔트리를 NULL로 초기화*/
void
process_close_file (int fd)
{
  struct thread *cur = thread_current();

  if (cur->pcb->fd_table[fd] != NULL && fd >= 2 && fd <= FD_MAX)
  {
    // if (cur->pcb->run_file == cur->pcb->fd_table[fd])
    //   cur->pcb->run_file = NULL;

    file_close(cur->pcb->fd_table[fd]); // 파일 디스크립터에 해당하는 파일을 닫음
    cur->pcb->fd_table[fd] = NULL;
  }
}

bool handle_mm_fault(struct spt_entry *_spte)
{
  struct frame *f;
  uint8_t *kpage;
  
  switch (_spte->type)
  {
    case VM_BIN: 
    case VM_FILE:
      f = falloc(PAL_USER);
      if (f == NULL) return false; 
      f->spte = _spte;
      kpage = f->kaddr;
      if (!load_file(kpage, _spte) || !install_page(_spte->vaddr, kpage, _spte->writable))
      {
          ffree(kpage);
          return false;
      }

      f->spte->is_loaded = true;
      f->spte->kpage = kpage; 
      return true;
    case VM_ANON:
    f = falloc(PAL_USER);
    if (f == NULL) return false; 
    f->spte = _spte;
    kpage = f->kaddr;
    swap_in(_spte, kpage);
    default: 
      return false;
  }

  return true;
}

/* mmap_file의 vme_list에 연결된 모든 vm_entry들을 제거
  vm_entry가리키는 가상 주소에 대한 물리 페이지가 존재하고, dirty하면 디
  스크에 메모리 내용을 기록 */
void 
do_unmap(struct mmap_file *mmap_file)
{
  struct list *spte_list = &mmap_file->spte_list;
  struct list_elem *spte_e;

  for (spte_e = list_begin(spte_list); spte_e != list_end(spte_list);)
  {
    struct spt_entry *spte = list_entry(spte_e, struct spt_entry, mmap_elem);
    
    if(pagedir_is_dirty(&thread_current()->pagedir, spte->vaddr))
    {
      file_write_at(spte->file, spte->vaddr, spte->read_bytes, spte->offset);
    }

    spte_e = list_remove(spte_e);
    delete_spte(&thread_current()->spt, spte); 
  }

  file_close(mmap_file->file);
  free(mmap_file);
}


/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  lock_acquire(&filesys_lock);
  file = filesys_open (file_name);
  if (file == NULL) 
  {
    printf ("load: %s: open failed\n", file_name);
    lock_release(&filesys_lock);
    goto done; 
  }

  t->pcb->run_file = file;
  file_deny_write(file);
  lock_release(&filesys_lock);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
  
  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Instead of loading file, just setup spte-JYL*/
      //init_spte_file (&thread_current ()->spt, upage, file, ofs, page_read_bytes, page_zero_bytes, writable);

      struct spt_entry *spte = (struct spt_entry *)malloc(sizeof(struct spt_entry));
      
      memset(spte, 0, sizeof(struct spt_entry));
      spte->type = VM_BIN;
      spte->file = file;
      spte->offset = ofs;
      spte->read_bytes = page_read_bytes;
      spte->zero_bytes = page_zero_bytes;
      spte->writable = writable;
      spte->vaddr = upage;

      insert_spte(&thread_current()->spt, spte);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  bool success = false;
  void *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
  struct frame* f = falloc(PAL_USER | PAL_ZERO);
  if (f == NULL) return false;
  void *kpage = f->kaddr;

  success = install_page(upage, kpage, true);
  if (success)
  {
    f->spte = (struct spt_entry *)malloc(sizeof(struct spt_entry));
    
    if (f->spte == NULL)
    {
      success = false;
      ffree(kpage);
    }
    else 
    {
      *esp = PHYS_BASE;

      memset(f->spte, 0, sizeof(struct spt_entry));
      f->spte->type = VM_ANON;
      f->spte->vaddr = upage;
      f->spte->writable = true;
      f->spte->is_loaded = true;
      f->spte->kpage = kpage;

      insert_spte(&thread_current()->spt, f->spte);

    }
  }
  else 
  {
    ffree(kpage);
  }

  return success;
}

// bool
// load_page (struct hash *spt, void *upage)
// {
//   struct spt_elem *spte = find_spte (spt, upage);

//   struct frame *f = falloc (upage, thread_current());
//   ASSERT (f->kpage != NULL)

//   bool was_holding_lock = lock_held_by_current_thread (&filesys_lock);

//   switch (spte->status)
//   {
//   case PAGE_ZERO:
//     memset (f->kpage, 0, PGSIZE);
//     break;
//   case PAGE_SWAP:
//     //swap_in(e, kpage);
//     break;
    
//   case PAGE_FILE:
//     if (file_read_at (spte->file, f->kpage, spte->read_bytes, spte->offset) != spte->read_bytes)
//     {
//       ffree (f->kpage);
//       sys_exit (-1);
//     }
    
//     memset (f->kpage + spte->read_bytes, 0, spte->zero_bytes);
//     break;

//   default:
//     sys_exit (-1);
//   }

//   // uint32_t *pagedir = thread_current ()->pagedir;

//   // // if (!pagedir_set_page (pagedir, upage, f->kpage, spte->writable))
//   // // {
//   // //   ffree (f->kpage);
//   // //   sys_exit (-1);
//   // // }

//   install_page(upage, f->kpage, spte->writable);

//   spte->kpage = f->kpage;
//   spte->status = PAGE_FRAME;
//   spte->in_memory = true;

//   return true;
// }

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */

bool install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

#endif