#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
#include "threads/thread.h"
#include <list.h>

/* System call initialization function */
void syscall_init(void);

/* System call functions declarations */
void sys_halt(void);
void sys_exit(int status);
tid_t sys_exec(const char *cmd_line);
int sys_wait(tid_t pid);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove(const char *file);
int sys_open(const char *file);
int sys_filesize(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);
int sys_mmap(int fd, void *addr);
void sys_munmap(int mapid);

/* Helper functions */
bool validate_addr(const void *addr);
bool validate_buffer(const void *buffer, unsigned size);
void get_argument(int *esp, int *arg, int count);

/* Lock for synchronizing file-related operations */
struct lock filesys_lock;

#endif /* userprog/syscall.h */