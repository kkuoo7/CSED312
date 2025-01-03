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
#include "threads/fp_arithm.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

#define MAX_DEPTH 8

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;


/* List of blocked processes by timer_sleep() function */
static struct list sleeping_list; 

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

int load_avg; 

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

static void set_MLFQS_priority(struct thread *t);

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

  list_init(&sleeping_list); // sleeping_list 초기화

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
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
  load_avg = 0;

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

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

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

#ifdef USERPROG
  t->parent_process = thread_current();

  t->pcb = palloc_get_page (PAL_ZERO); // for avoding fragmentation
  if (t->pcb == NULL) {    
    return TID_ERROR;
  }

  t->pcb->tid = thread_tid();
  t->pcb->fd_table = palloc_get_page (PAL_ZERO);
  if (t->pcb->fd_table == NULL) {
    palloc_free_page (t->pcb);
    return TID_ERROR;
  }

  t->pcb->is_exited = false;
  t->pcb->is_loaded = false;
  t->pcb->exit_code = -1;
  t->pcb->waited = 0;

  t->pcb->fd_count = 2;
  t->pcb->run_file = NULL;

  sema_init (&(t->pcb->sema_wait), 0);
  sema_init (&(t->pcb->sema_load), 0);

  list_push_back (&(t->parent_process->children), &(t->child_elem));
#endif

  /* Add to run queue. */
  thread_unblock (t);

  /* If the newly created thread has a higher priority than the current thread,
   it preempts the CPU. */
  thread_preempt();

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

  /* ready_list can be accesed in interrupt context by thread_wakeup(). */
  old_level = intr_disable ();

  ASSERT (t->status == THREAD_BLOCKED);

 /* For priority scheduling: insert elements in descending order of priority. */
  list_insert_ordered(&ready_list, &t->elem, (list_less_func *) compare_priority_desc, NULL);
  t->status = THREAD_READY;

  intr_set_level (old_level);
}

void
thread_sleep(int64_t wakeup_time)
{
  /* sleeping_list can be accessed in interrupt context by thread_wakeup function. 
  locking on sleeping_list may lead to dead lock situations if timer interrupt occurs
  in the middle of thread_sleep function. */
  enum intr_level old_level = intr_disable(); 

  struct thread *cur = thread_current();

  /* idle thread must not be put to sleep. Remember that the idle thread is selected by 
  the scheduler when there are no threads in the ready_list. */
  ASSERT (cur != idle_thread); 

  cur->wake_up_time = wakeup_time; 

  /* insert elements to sleeping_list in ascending order of wake_up_time */
  list_insert_ordered(&sleeping_list, &cur->elem, (list_less_func *) compare_wakeup_ticks, NULL); 

  /* change the state of sleeping thread into THREAD_BLOCK. Then call schedule(). */
  thread_block();

  intr_set_level(old_level);
}

void 
thread_wakeup(int64_t ticks) 
{
  struct thread *t;
  struct list_elem *e; 

  for(e = list_begin(&sleeping_list); e != list_end(&sleeping_list);)
  {
    t = list_entry(e, struct thread, elem);
    ASSERT(is_thread(t))

    if(t->wake_up_time > ticks) // too early to wake up
      break; 
    else // time to wake up
    {
      e = list_remove(e); // Return elem->next; Thus we don't need to call list_next(e);
      thread_unblock(t); // change state of t into THREAD_READY.
    }
  }
}

void
thread_preempt()
{
  if(!list_empty(&ready_list))
  {
    struct thread *cur = thread_current();
    struct thread *next = list_entry(list_front(&ready_list), struct thread, elem);

    if(cur->priority < next->priority)
      thread_yield();
  }
}

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
  
  /* Check the next_thread_to_run function. 
  idle thread should not be inserted in ready_list. */
  if (cur != idle_thread)
    list_insert_ordered(&ready_list, &cur->elem, (list_less_func *) compare_priority_desc, NULL);
 
  cur->status = THREAD_READY;
  schedule ();
  
  intr_set_level (old_level);
}

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

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{

  /* MLFQS does not allow direct changes to priority. It aims to make a fair and balanced system. 
  While priority inversion can occur, MLFQS does not directly address this issue. */
  if (thread_mlfqs)
    return;

  thread_current ()->original_priority = new_priority;
  thread_reflect_donation_list();

  /* After change the priority of current thread, Check whether 
  it has lower prirority than ready_list */
  thread_preempt();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Consider the nested donation priority */
void
thread_donate_priority(struct thread *holder, int depth)
{
  depth = 0; 
  struct thread *trying_thread = thread_current();

  for(; trying_thread->lock_wait != NULL && depth < MAX_DEPTH; depth++, 
    trying_thread = trying_thread->lock_wait->holder)
  {
    if(trying_thread->priority > trying_thread->lock_wait->holder->priority)
      trying_thread->lock_wait->holder->priority = trying_thread->priority;
  }

}

/* Consider the nested donation situation */
void thread_donate_priority_test(void)
{
  int depth = 0;
  struct thread *trying_thread = thread_current();
  struct thread *holder;

  while(trying_thread->lock_wait != NULL && depth < MAX_DEPTH)
  {
    holder = trying_thread->lock_wait->holder;
    
    if (holder->priority < trying_thread->priority)
    {
      holder->priority = trying_thread->priority;
    }

    trying_thread = holder;
    depth++;   
  }
}

void thread_remove_donation_elem(struct thread *releasing_thread, struct lock *lock)
{
    struct list_elem *e; 
    struct thread *donated_thread; 

    for (e = list_begin(&releasing_thread->donation_list); e != list_end(&releasing_thread->donation_list);)
    {
      donated_thread = list_entry(e, struct thread, donation_elem);
      ASSERT(is_thread(donated_thread));

      if (donated_thread->lock_wait == lock)
      {
        e = list_remove(e);
        donated_thread->lock_wait = NULL;
      }
      else 
        e = list_next(e);
    }
}

/* Addressing multiple donation situation */
void thread_reflect_donation_list(void)
{
  struct thread *cur = thread_current();
  cur->priority = cur->original_priority;

  if(!list_empty(&cur->donation_list)) 
  {
    struct thread *max = list_entry(list_max(&cur->donation_list, compare_priority_desc, NULL), 
                                      struct thread, donation_elem);

    if (max->priority > cur->priority)
      cur->priority = max->priority;
  }
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  struct thread *cur = thread_current();
  if (cur == idle_thread)
    return;

  enum intr_level old_level = intr_disable();

  cur->nice = nice;
  set_MLFQS_priority(cur);
  // list_sort(&ready_list, compare_priority_desc, NULL); 
  
  thread_preempt();

  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  enum intr_level old_level = intr_disable(); 
  old_level = intr_disable(); 

  int nice = thread_current()->nice;

  intr_set_level(old_level);

  return nice;
}

//recent_cpu = decay * recent_cpu + nice
//decay = 2*load_avg / (2*load_avg + 1)
void 
calc_recent_cpu(struct thread *t)
{
  //nice 와 decay 를 이용해 쓰레드의 recent_cpu를 계산
  int decay = fp_div(fp_mul_int(load_avg, 2), fp_add_int(fp_mul_int(load_avg, 2), 1));
  t->recent_cpu = fp_add_int(fp_mul(decay, t->recent_cpu) , t->nice);
}

/* increment current therad's recent_cpu */
void 
increase_recent_cpu()
{
  struct thread *cur = thread_current();

  if(cur == idle_thread) 
    return;

  cur->recent_cpu = fp_add_int(cur->recent_cpu, 1);
}

/*load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads */
void 
calc_load_avg()
{
  //load_avg 계산
  int ready_threads;

  /*ready_threads = # of threads in ready_list + # of running thread (0 if currnet thread is idle thread, else 1)*/
  if(thread_current() != idle_thread) 
    ready_threads = list_size(&ready_list) + 1;
  else 
    ready_threads = list_size(&ready_list);
 
  load_avg = fp_add(fp_mul(fp_div(int_to_fp(59),int_to_fp(60)), load_avg), fp_div_int(int_to_fp(ready_threads), 60)); //1/60 * ready_threads
}

/* increase all thread's recent_cpu value */
void 
recent_cpu_update()
{
  struct thread *t;

  for(struct list_elem *e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
  {
    t = list_entry(e, struct thread, allelem);
    calc_recent_cpu(t);
  }
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  enum intr_level old_level; 
  old_level = intr_disable(); 

  int return_load_avg = fp_to_int_rounding(fp_mul_int(load_avg, 100));

  intr_set_level(old_level);
  return return_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  enum intr_level old_level;
  old_level = intr_disable();

  int return_recent_cpu = fp_to_int_rounding(fp_mul_int(thread_current()->recent_cpu, 100));
  intr_set_level(old_level);

  return return_recent_cpu;
}

void set_MLFQS_priority(struct thread *t)
{
  if(t == idle_thread) return;

  //int mlfqs_priority = PRI_MAX - fp_to_int_rounding(fp_div_int(t->recent_cpu, 4) - int_to_fp(t->nice * 2));
  int mlfqs_priority = fp_to_int_rounding(fp_sub(fp_sub(int_to_fp(PRI_MAX), fp_div_int(t->recent_cpu, 4)), fp_mul_int(int_to_fp(t->nice), 2)));
  
  if(mlfqs_priority > PRI_MAX) 
    t->priority = PRI_MAX;
  else if(mlfqs_priority < PRI_MIN) 
    t->priority = PRI_MIN;//범위 넘어갈 경우
  else 
    t->priority = mlfqs_priority;
}

/* Update MLFQS priority of all threads then, sort ready_list 
in descending order of priority */
void MLFQS_priority_update(){ //모든 쓰레드를 MLFQS priority로 업데이트

  struct thread *t;

  for(struct list_elem *e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
  {
    t = list_entry(e, struct thread, allelem);
    set_MLFQS_priority(t);
  }

  list_sort(&ready_list, compare_priority_desc, NULL);
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
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->original_priority = priority;
  t->nice = 0;
  t->recent_cpu = 0;
  t->magic = THREAD_MAGIC;

  list_init(&t->donation_list);
  list_init(&t->children);

  list_init(&t->mmap_list);

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
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
      // palloc_free_page (prev);
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


bool
compare_wakeup_ticks( const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);

  return t1->wake_up_time < t2->wake_up_time;
}

bool
compare_priority_desc(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);

  return t1->priority > t2->priority;
}

struct thread *
get_thread_by_tid(tid_t tid) {
    struct list_elem *e;

    /* 모든 스레드 리스트(all_list)를 순회하며 tid가 일치하는 스레드를 찾음 */
    for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) 
    {
        struct thread *t = list_entry(e, struct thread, allelem);
        if (t->tid == tid) 
        {
            return t;  
        }
    }

    return NULL;  // 해당 tid를 가진 스레드가 없을 경우 NULL 반환
}
