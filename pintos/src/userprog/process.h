#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H
#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

mapid_t mmfiles_insert (void *, struct file*, int32_t);
void mmfiles_remove (mapid_t);

#endif /* userprog/process.h */
