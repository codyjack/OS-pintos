#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "userprog/process.h"

/* the lock used by syscalls involving file system to ensure only one thread
   at a time is accessing file system */
struct lock fs_lock;

void syscall_init (void);
bool is_valid_ptr (const void *);
void close_file_by_owner (tid_t);
#endif /* userprog/syscall.h */
