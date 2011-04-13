#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int pid_t;

void syscall_init (void);
void close_file_by_owner (int);
#endif /* userprog/syscall.h */
