#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
/* ##> Our implementation */
/* In order to use timer_ticks() */
#include "devices/timer.h"
/* Fixed point arithmetic */
#include "threads/fixed-point.h"
/* <## */
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b


/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;


/* ##> Our implementation
   List of processes in THREAD_BLOCK state, that is, processes
   that are in sleep status. */
static struct list sleep_list;

/* load_avg for advanced priority. Fixed-point number */
static int load_avg;
/* <##*/

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
/* ##> Our implementation */
static bool sleep_ticks_less(const struct list_elem *a_,
                             const struct list_elem *b_,
                             void *aux UNUSED);
static bool priority_more(const struct list_elem *a_,
                          const struct list_elem *b_,
                          void *aux UNUSED);
/* <## */
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  /* ##> our implementation*/
  list_init (&sleep_list);
  /* <##*/

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  /* ##> Our implementation */
  /* At system boot, it is initialized to 0 */
  load_avg = 0;
  /* <## */
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();

  /* ##> our implementation
   * check if there are threads need to be waken up */
  thread_wakeup();
  /* <##*/
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  /* ##> Our implemented */
  if (thread_mlfqs)
    {
      calculate_recent_cpu (t, NULL);
      calculate_advanced_priority (t, NULL);
      thread_calculate_recent_cpu ();
      thread_calculate_advanced_priority ();
    }

  if (t->priority > thread_current ()-> priority)
    {
      thread_yield_current (thread_current ());
    }
  /* <## */
  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  /* Original implementation
   * list_push_back (&ready_list, &t->elem);
   */
  /* ##> Our implementation */
  /* For PRIORITY propose
   * Change the ready_list to an ordered list in order to make sure that
   * the thread with highest priority in the ready list get run first
   */
  list_insert_ordered(&ready_list, &t->elem, priority_more, NULL);
  /* <## */
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* ##> our implementation*/
/* Returns true if thread A->sleep_ticks is less than thread B->sleep_ticks,
 * false otherwise.
 * When used in list_insert_ordered, a list_elem will be insert in ascending
 * order according to thread->sleep_ticks. If more than one list_elem have the
 * same sleep_ticks, then insert the new one at the BEGINNING of the list_elems
 * with the same sleep_ticks.
 */
static bool
sleep_ticks_less (const struct list_elem *a_, const struct list_elem *b_,
                  void *aux UNUSED)
{
  ASSERT (a_ != NULL);
  ASSERT (b_ != NULL);
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);

  return a->sleep_ticks < b->sleep_ticks;
}

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

/* Put current thread to sleep for a given ticks time */
void
thread_sleep (int64_t ticks)
{
  /* get current time ticks
   * set ticks in thread structe
   * put thread into sleep queue
   * turn interupts off
   * put to block state
   */
  enum intr_level old_level; // In order to reset the interputs state afterward

  /* Get current thread */
  struct thread *cur = thread_current ();
  /* Ask to sleep for 0 ticks */
  if (ticks <= 0)
    return;

  ASSERT(cur->status == THREAD_RUNNING);

  /* Get and set time ticks*/
  cur->sleep_ticks = ticks + timer_ticks ();
  /* Turn interupts off before operating on the current thread */
  old_level = intr_disable ();
  /* Put the thread into a sleep queue, whose element's sleep_ticks is in
   * ascending order */
  list_insert_ordered (&sleep_list, &cur->elem, sleep_ticks_less, NULL);

  /* Block the thread*/
  thread_block ();
  /* Reset the interupts according to its level before turning off*/
  intr_set_level (old_level);
}

/* Weak up a thread whose sleep_ticks is no bigger than the current ticks.
 * Here weak up means put a thread from sleep queue to ready queue and set
 * its thread_status from THREAD_BLOCKED to THREAD_READY which can be done
 * by calling thread_unblock(struct thread*)
 */
void
thread_wakeup (void)
{
  /* check threads in sleep queue by their sleep_ticks 
   * if current sleep_ticks <= timer_ticks(), which is current ticks
   * unblock it and remove it from sleep queue
   * do these till found a thread whose sleep_ticks > timer_ticks()
   */
  struct list_elem *elem_cur; // Current element in the list
  struct list_elem *elem_next; // Next element connected to the current one
  struct thread *t;
  enum intr_level old_level;

  if (list_empty (&sleep_list))
    return;

  elem_cur = list_begin (&sleep_list);
  while (elem_cur != list_end (&sleep_list))
    {
      elem_next = list_next (elem_cur);
      t = list_entry (elem_cur, struct thread, elem);
      if (t->sleep_ticks > timer_ticks())
        break;

      /* Remove the thread from sleep queue and unblock it */
      old_level = intr_disable ();
      list_remove (elem_cur);
      thread_unblock (t);
      intr_set_level (old_level);

      elem_cur = elem_next;
    }
}

/* <##*/


/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) {
    /* Original implementation
     * list_push_back (&ready_list, &cur->elem);
     */
    /* ##> Our implementation
     * For PRIORITY propose
     * Change the ready_list to an ordered list in order to make sure that
     * the thread with highest priority in the ready list get run first
     */
    list_insert_ordered(&ready_list, &cur->elem, priority_more, NULL);
    /* <## */
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* ##> Our implementation */
/* Let the cur thread Yields the CPU. The cur thread is not put to sleep
 * and may be schedule again immediately at the scheduler's whim
 */
void
thread_yield_current (struct thread *cur)
{
  ASSERT (is_thread (cur));
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) {
    /* For PRIORITY propose
     * Change the ready_list to an ordered list in order to make sure that
     * the thread with highest priority in the ready list get run first
     */
    list_insert_ordered(&ready_list, &cur->elem, priority_more, NULL);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}
/* <## */

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY.
 * ++ "If the current thread no longer has the highest priority, yields"
 */
void
thread_set_priority (int new_priority)
{
  thread_given_set_priority (thread_current (), new_priority, false);
}

/* ##> Our implementation */
void
thread_given_set_priority (struct thread *cur, int new_priority,
                           bool is_donated)
{
  enum intr_level old_level;
  old_level = intr_disable();

  ASSERT (new_priority >= PRI_MIN && new_priority <= PRI_MAX);
  ASSERT (is_thread (cur));

   /* if this operation is a not donatation
    *   if the thread has been donated and the new priority is less 
    *   or equal to the donated priority, we should delay the process
    *   by preserve it in priority_original.
    * otherwise, just do the donation, set priority to the donated
    * priority, and mark the thread as a donated one.
    */ 
   if (!is_donated) 
     {
       if (cur->is_donated && new_priority <= cur->priority) 
          cur->priority_original = new_priority;
       else
          cur->priority = cur->priority_original = new_priority;
     }
   else 
     {
	cur->priority = new_priority;
        cur->is_donated = true;
     }


  /* If the current thread's status is THREAD_READY, then just reinsert it
   * to the ready_list in order to keep the ready_list in order; if its status
   * is THREAD_RUNNING, then compare its priority with the largest one's
   * priority in the ready_list: if the current one's is smaller, then yields
   * the CPU.
   */
  if (cur->status == THREAD_READY)
    {
      list_remove (&cur->elem);
      list_insert_ordered (&ready_list, &cur->elem, priority_more, NULL);
    }
  else if (cur->status == THREAD_RUNNING &&
           list_entry (list_begin (&ready_list),
                       struct thread,
                       elem
                       )->priority > cur->priority
           )
    {
      thread_yield_current (cur);
    }
  intr_set_level (old_level);
  /* <## */
}

/* ##> Our implementation */
/* Calculate BSD scheduling priority */
void
thread_calculate_advanced_priority (void)
{
  calculate_advanced_priority (thread_current (), NULL);
}

/* Calculate priority for all threads in all_list.
 *  It is also recalculated once every fourth clock tick, for every thread.
 */
void
calculate_advanced_priority_for_all (void)
{
  thread_foreach (calculate_advanced_priority, NULL);
  /* resort ready_list */
  if (!list_empty (&ready_list))
    {
      list_sort (&ready_list, priority_more, NULL);
    }
}

/* Calculate advanced priority.
 * Thread priority is calculated initially at thread initialization.
 * It is also recalculated once every fourth clock tick, for every thread.
 * In either case, it is determined by the formula
 * priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
 */
void
calculate_advanced_priority (struct thread *cur, void *aux UNUSED)
{
  ASSERT (is_thread (cur));
  if (cur != idle_thread)
    {
      /* convert to integer nearest for (recent_cpu / 4) instead
       * of the whole priority.
       */
      cur->priority = PRI_MAX -
        CONVERT_TO_INT_NEAREST (DIV_INT (cur->recent_cpu, 4)) -  cur->nice * 2;
      /* Make sure it falls in the priority boundry */
      if (cur->priority < PRI_MIN)
        {
          cur->priority = PRI_MIN;
        }
      else if (cur->priority > PRI_MAX)
        {
          cur->priority = PRI_MAX;
        }
    }
}

/* Calculate recent_cpu for a thread */
void
thread_calculate_recent_cpu (void)
{
  calculate_recent_cpu (thread_current (), NULL);
}

/* Once per second the value of recent_cpu is recalculated
 * for every thread (whether running, ready, or blocked)
 */
void
calculate_recent_cpu_for_all (void)
{
  thread_foreach (calculate_recent_cpu, NULL);
}

/* Calculate recent_cpu
 * recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice
 * Assumptions made by some of the tests require that these recalculations
 * of recent_cpu be made exactly when the system tick counter reaches a
 * multiple of a second, that is, when timer_ticks () % TIMER_FREQ == 0,
 * and not at any other time.
 *
 * The value of recent_cpu can be negative for a thread with a negative nice
 * value. Do not clamp negative recent_cpu to 0.

 * You may need to think about the order of calculations in this formula.
 * We recommend computing the coefficient of recent_cpu first, then
 * multiplying. Some students have reported that multiplying load_avg by
 * recent_cpu directly can cause overflow.
 */
void
calculate_recent_cpu (struct thread *cur, void *aux UNUSED)
{
  ASSERT (is_thread (cur));
  if (cur != idle_thread)
    {
      /* load_avg and recent_cpu are fixed-point numbers */
      int load = MULT_INT (load_avg, 2);
      int coefficient = DIVIDE (load, ADD_INT (load, 1));
      cur->recent_cpu = ADD_INT (MULTIPLE (coefficient, cur->recent_cpu),
                                 cur->nice);
    }
}

/* Calculate load_avg.
 * load_avg, often known as the system load average, estimates the average
 * number of threads ready to run over the past minute. Like recent_cpu, it is
 * an exponentially weighted moving average. Unlike priority and recent_cpu,
 * load_avg is system-wide, not thread-specific. At system boot, it is
 * initialized to 0. Once per second thereafter, it is updated according to
 * the following formula:
 * load_avg = (59/60)*load_avg + (1/60)*ready_threads
 * where ready_threads is the number of threads that are either running or
 * ready to run at time of update (not including the idle thread).

 * Because of assumptions made by some of the tests, load_avg must be updated
 * exactly when the system tick counter reaches a multiple of a second, that
 * is, when timer_ticks () % TIMER_FREQ == 0, and not at any other time.
 */
void
calculate_load_avg (void)
{
  struct thread *cur;
  int ready_list_threads;
  int ready_threads;

  cur = thread_current ();
  ready_list_threads = list_size (&ready_list);

  if (cur != idle_thread)
    {
      ready_threads = ready_list_threads + 1;
    }
  else
    {
      ready_threads = ready_list_threads;
    }
  load_avg = MULTIPLE (DIV_INT (CONVERT_TO_FP (59), 60), load_avg) +
    MULT_INT (DIV_INT (CONVERT_TO_FP (1), 60), ready_threads);
}

/* returns the current thread's priority. */
int
thread_get_priority (void) 
{
  /* Original implementation */
  return thread_current ()->priority;
  /* ##> Our implementation */
  /* Returns the current thread's priority.
   * In the presence of priority donation, returns the higher (donated)
   * priority.
   */
}

/* Sets the current thread's nice value to NICE. */
/* Sets the current thread's nice value to new_nice and recalculates
 * the thread's priority based on the new value (see section B.2 Calculating
 * Priority). If the running thread no longer has the highest priority, yields.
 */
void
/* Original implementation */
/* thread_set_nice (int nice UNUSED) */
thread_set_nice (int new_nice)
{
  /* Not yet implemented. */
  /* ##> Our implementation */
  ASSERT (new_nice >= NICE_MIN && new_nice <= NICE_MAX);
  struct thread *cur;

  cur = thread_current ();
  cur->nice = new_nice;

  // thread_calculate_recent_cpu ();
  thread_calculate_advanced_priority ();
  /* If the current thread's status is THREAD_READY, then just reinsert it
   * to the ready_list in order to keep the ready_list in order; if its status
   * is THREAD_RUNNING, then compare its priority with the largest one's
   * priority in the ready_list: if the current one's is smaller, then yields
   * the CPU.
   */
  if (cur != idle_thread)
    {
      if (cur->status == THREAD_READY)
        {
          enum intr_level old_level;
          old_level = intr_disable ();
          list_remove (&cur->elem);
          list_insert_ordered (&ready_list, &cur->elem, priority_more, NULL);
          intr_set_level (old_level);
        }
      else if (cur->status == THREAD_RUNNING &&
               list_entry (list_begin (&ready_list),
                           struct thread,
                           elem
                           )->priority > cur->priority
               )
        {
          thread_yield_current (cur);
        }
    }
  /* <## */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  /* Original implementation */
  /* return 0; */
  /* ##> Our implementation */
  return thread_current ()->nice;
  /* <## */
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  /* Original implementation */ 
  /* return 0; */
  /* ##> Our implementation */
  return CONVERT_TO_INT_NEAREST (MULT_INT (load_avg, 100));
  /* <## */
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  /* Original implementation */
  /* return 0; */
  /* ##> Our implementation */
  return CONVERT_TO_INT_NEAREST (MULT_INT (thread_current ()->recent_cpu,
                                           100));
  /* <## */
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  /* ##> Our implementation */
  /* For donation */
  /* if (!thread_mlfqs) */
  /*   { */
  t->priority_original = priority;
  t->is_donated = false;
  list_init (&t->locks);
  t->lock_blocked_by = NULL;
    /* } */
  if (thread_mlfqs)
    {
      /* The initial thread starts with a nice value of zero. Other threads
       * start with a nice value inherited from their parent thread
       */
      /* The initial value of recent_cpu is 0 in the first thread created,
       * or the parent's value in other new threads.
       */
      if (t == initial_thread)
        {
          t->nice = NICE_DEFAULT;
          t->recent_cpu = RECENT_CPU_BEGIN;
        }
      else
        {
          t->nice = thread_get_nice ();
          t->recent_cpu = thread_get_recent_cpu ();
        }
    }
  /* <## */
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
