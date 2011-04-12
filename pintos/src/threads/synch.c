/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* ##> Our implementation */
static bool priority_more (const struct list_elem *a_,
                           const struct list_elem *b_,
                           void *aux UNUSED);
static bool priority_sema_more (const struct list_elem *a_,
                                const struct list_elem *b_,
                                void *aux UNUSED);
static bool lock_priority_more (const struct list_elem *a_,
                                const struct list_elem *b_,
                                void *aux UNUSED);
/* Returns true if thread A->priority is bigger than thread B->priority,
 * false otherwise.
 * When used in list_insert_ordered, a list_elem will be insert in descending
 * order according to thread->priority. If more than one list_elem have the
 * same priority, then insert the new one at the END of the list_elem with the
 * same priority.
 */
static bool
priority_more (const struct list_elem *a_, const struct list_elem *b_,
               void *aux UNUSED)
{
  ASSERT (a_ != NULL);
  ASSERT (b_ != NULL);
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);

  return a->priority > b->priority;
}

/* Compare lock's priority_lock
 * It's in descending order. If multiple locks have the same priority_lock,
 * put the newer one in the front.
 */
static bool
lock_priority_more (const struct list_elem *a_, const struct list_elem *b_,
                    void *aux UNUSED)
{
  ASSERT (a_ != NULL);
  ASSERT (a_ != NULL);
  const struct lock *a = list_entry (a_, struct lock, elem_lock);
  const struct lock *b = list_entry (b_, struct lock, elem_lock);

  return a->priority_lock >= b->priority_lock;
}
/* <## */

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      /* Original implementation */
      /* list_push_back (&sema->waiters, &thread_current ()->elem); */
      /* ##> Our implementation */
      /* For priority propose
       * Change waiters to be a descending order list according to
       * thread->priority
       */
      list_insert_ordered (&sema->waiters, &thread_current ()->elem,
                           priority_more, NULL);
      /* <## */
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
  struct thread *next = NULL; /* The top thread in the waiters list */
  struct thread *cur = thread_current ();

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  while (!list_empty (&sema->waiters))
    {
    /* Original implementation */
    /* thread_unblock (list_entry (list_pop_front (&sema->waiters),
     *                             struct thread, elem));
     */
    /* ##> Our implementation */
    next = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
    thread_unblock (next);
    /* <## */
    }
  sema->value++;
  /* ##> Our implementation */
  /* preempt */
  if (next != NULL && next->priority > cur->priority)
    {
      thread_yield_current (cur);
    }
  /* <## */
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  /* ##> Our implementation */
  /* Set priority_lock to a value out of priority boundary */
  lock->priority_lock = PRIORITY_FAKE;
  /* <## */
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  /* ##> Our implementation */
  struct thread *cur;
  struct thread *lock_holder;
  struct lock *lock_next;
  int lock_iter;
  enum intr_level old_level;

  old_level = intr_disable ();
  cur = thread_current ();
  lock_holder = lock->holder;
  lock_next = lock;
  lock_iter = 0;

  if (lock_holder != NULL)
    cur->lock_blocked_by = lock;

  while (!thread_mlfqs && lock_holder != NULL &&
         lock_holder->priority < cur->priority)
    {
      thread_given_set_priority (lock_holder, cur->priority, true);
      /* Priority_lock is the highest priority in its waiters list */
      if (lock_next->priority_lock < cur->priority)
        {
          lock_next->priority_lock = cur->priority;
        }
      /* Nest donation: find the next lock that locks the current
       * lock_holder
       */
      if (lock_holder->lock_blocked_by != NULL && lock_iter < LOCK_LEVEL)
        {
          lock_next = lock_holder->lock_blocked_by;
          lock_holder = lock_holder->lock_blocked_by->holder;
          lock_iter ++;
        }
      else
        break;
    }
  /* <## */
  sema_down (&lock->semaphore);
  /* Original implementation */
  /* lock->holder = thread_current (); */

  /* ##> Our implementation */
  lock->holder = cur;
  if (!thread_mlfqs)
    {
      /* After getting this lock, reset lock_blocked_by and add this lock
       * to the locks list
       */
      cur->lock_blocked_by = NULL;
      /* Add this lock to the thread's lock holding list */
      list_insert_ordered (&cur->locks, &lock->elem_lock,
                           lock_priority_more, NULL);
    }
  intr_set_level (old_level);
  /* <## */
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    {
      lock->holder = thread_current ();
    }
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  /* ##> Our implementation */
  struct thread *cur;
  enum intr_level old_level;
  cur = thread_current ();
  old_level = intr_disable ();
  /* <## */

  lock->holder = NULL;
  sema_up (&lock->semaphore);
  /* ##> Our implementation */
  if (!thread_mlfqs)
    {
      list_remove (&lock->elem_lock);
      lock->priority_lock = PRIORITY_FAKE;
      /* Current thread only holds this lock */
      if (list_empty (&cur->locks))
        {
          cur->is_donated = false;
          thread_set_priority (cur->priority_original);
        }
      /* Multiple donation situation */
      else
        {
          /* locks list are sorted in descending order by the thread priority
           * in a lock's semaphore's waiters
           */
          struct lock *lock_first;
          lock_first = list_entry (list_front (&cur->locks), struct lock,
                                   elem_lock);
          /* If there is at least one thread in the waiters list, then donate
           * the lock's priority_lock to current thread
           */
          if (lock_first->priority_lock != PRIORITY_FAKE)
            {
              thread_given_set_priority (cur, lock_first->priority_lock, true);
            }
          /* If a lock's semaphore's waters list is empty, which mean's there
           * is no thead acquiring this lock, reset current priority to its
           * original priority
           */
          else
            {
              thread_set_priority (cur->priority_original);
            }
        }
    }
  intr_set_level (old_level);
  /* <## */
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    /* ##> Our implementation */
    int priority_sema;
    /* <## */
  };

/* ##> Our implementation */
/* Compare the two input semaphore_elem according to their elem's priority
 * If the first one's is bigger than the second one's, then return true,
 * otherwise, return false
 */
static bool
priority_sema_more (const struct list_elem *a_, const struct list_elem *b_,
                    void *aux UNUSED)
{
  ASSERT (a_ != NULL);
  ASSERT (b_ != NULL);

  const struct semaphore_elem *a = list_entry (a_, struct semaphore_elem,
                                               elem);
  const struct semaphore_elem *b = list_entry (b_, struct semaphore_elem,
                                               elem);

  return (a->priority_sema > b->priority_sema);
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  /* Old implementation */
  /* list_push_back (&cond->waiters, &waiter.elem); */

  /* ##> Our implementation */
  /* For priority propose
   * Change cond->waiters to be a descending order list according to
   * priority
   */
  waiter.priority_sema = thread_current ()->priority;
  list_insert_ordered (&cond->waiters, &waiter.elem, priority_sema_more, NULL);
  /* <## */
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
