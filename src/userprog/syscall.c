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

#define USER_START_ADDRESS 0x8048000

void check_address (void *addr)
{
  if (addr >= USER_START_ADDRESS && is_user_vaddr(addr))
    return;

  sys_exit(-1);
}


void 
get_argument (int *esp, int *arg, int count)
{
  for (int i = 0; i < count; i++)
  {
    check_address(esp + i + 1);
    arg[i] = *(esp + i + 1);
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
  check_address(f->esp);

  int arg[3];
  int syscall_number = *(int *)(f->esp);

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
      check_address ((const char *) arg[0]);
      f->eax = sys_exec ((const char *) arg[0]);
      break;

    case SYS_WAIT:
      get_argument (f->esp, arg, 1);
      f->eax = sys_wait ((tid_t) arg[0]);
      break;

    case SYS_CREATE:
      get_argument (f->esp, arg, 2);      
      check_address ((const char *) arg[0]);
      f->eax = sys_create ((const char *) arg[0], (unsigned int) arg[1]);
      break;

    case SYS_REMOVE:
      get_argument (f->esp, arg, 1);
      check_address ((const char *) arg[0]);
      f->eax = sys_remove ((const char *) arg[0]);
      break;

    case SYS_OPEN:
      get_argument (f->esp, arg, 1);
      check_address ((const char *) arg[0]);
      f->eax = sys_open ((const char *) arg[0]);
      break;

    case SYS_FILESIZE:
      get_argument (f->esp, arg, 1);
      f->eax = sys_filesize (arg[0]);
      break;
      
    case SYS_READ:
      get_argument (f->esp, arg, 3);
      check_address(arg[1]);
      f->eax = sys_read (arg[0], (void *) arg[1], (unsigned int) arg[2]);
      break;

    case SYS_WRITE:
      get_argument (f->esp, arg, 3);
      check_address(arg[1]);
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
