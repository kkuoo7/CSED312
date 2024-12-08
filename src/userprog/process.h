#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/interrupt.h"

struct pcb {
  tid_t tid;                      // Thread ID of the process
  struct thread *parent_process;  // Pointer to the parent process

  bool is_exited;                 // True if the process has exited
  bool is_loaded;                 // True if the process has loaded successfully
  
  struct semaphore sema_load;     // Semaphore for parent to wait during loading
  struct semaphore sema_wait;     // Semaphore for parent to wait on child

  int exit_code;                  // Exit status of the process
  int waited; 
  
  struct file **fd_table;         // File descriptor table for open files
  int next_fd;                    // Index for file descriptor 
  int fd_count;                   // Number of file descriptors in use
  struct file *run_file;        // The file representing the executable (to deny writes)
}; 

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

int parse_arguments (char *cmd, char **argv);
void parse_filename (char * cmd);
void init_stack_arg (char **argv, int argc, void **esp);
void argument_stack (char **argv, int argc, struct intr_frame *if_);
struct thread*  get_child_process(tid_t child_tid);
void remove_child_process(tid_t child_tid);
int process_add_file (struct file *f);
struct file* process_get_file (int fd);
void process_close_file (int fd);

bool load_page (struct hash *spt, void *upage);

#endif /* userprog/process.h */