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
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
// static struct list ready_list;

/* Multilevel feedback queue. Array of pointers to lists of threads. */
static struct list multilevel_queue[64];

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
   Controlled by kernel command-line option "-mlfqs". */
bool thread_mlfqs;

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
static void update_priority_bsd (struct thread *t, void *aux);
static void update_recent_cpu (struct thread *t, void *aux);
static void update_load_avg (void);

static fixed_point_t load_avg;

/*  Compare two threads based on their priority value.
    Return true if the priority of thread a is 
    greater than the priority of thread b. */
bool 
sort_threads_by_priority(const struct list_elem *a_,
                                               const struct list_elem *b_,
                                               void *aux UNUSED) {
  struct thread *a = list_entry(a_, struct thread, elem);
  struct thread *b = list_entry(b_, struct thread, elem);
  return a->effective_priority > b->effective_priority;
}

/* The priority of the current thread is checked against other,
   If other has a higher priority, then the current thread yields. */
void
yield_if_necessary(struct thread *other) {
  if (other->effective_priority > thread_current()->effective_priority) {
    thread_yield();
  }
}

/* Initialises the lists in the multilevel feedback queue. */
void
init_multilevel_queue (void) {
  for (int i = PRI_MIN; i <= PRI_MAX; i++) {
    list_init(&multilevel_queue[i]);
  }
}

/* Returns the priority value of the thread with the greatest priority in 
   the multilevel feedback queue. Disables interrupts while traversing queue. */ 
int
thread_max_priority (void)
{
  enum intr_level old_level = intr_disable ();

  int res = PRI_MIN - 1;
  for (int i = PRI_MAX; i >= PRI_MIN; i--)
    if (!list_empty (&multilevel_queue[i]))
      {
        res = i;
        break;
      }

  intr_set_level (old_level);
  return res;
}

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
  init_multilevel_queue();
  list_init (&all_list);

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

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Returns the number of threads currently in the ready list. 
   Disables interrupts to avoid any race-conditions on the ready list. */
size_t
threads_ready (void)
{
  enum intr_level old_level = intr_disable ();
  size_t cnt = 0;
  for (int i = PRI_MIN; i <= PRI_MAX; i++) 
  {
    cnt += list_size (&multilevel_queue[i]);
  }
  intr_set_level (old_level);
  return cnt;
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
  else if (t->is_user)
    user_ticks++;
#endif
  else {
    kernel_ticks++;
  }

  if (thread_mlfqs)
  {
    if (t != idle_thread)
    {
      t->recent_cpu += int_to_fixed_point (1);
      update_priority_bsd (t, NULL);
    }

    if (timer_ticks () % TIMER_FREQ == 0)
    {
      update_load_avg ();
      thread_foreach (&update_recent_cpu, NULL);
      thread_foreach (&update_priority_bsd, NULL);
    }

    if (t->effective_priority < thread_max_priority ())
      intr_yield_on_return ();
  }

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

  if (thread_mlfqs) 
  {
    t->recent_cpu = thread_current()->recent_cpu;
    update_priority_bsd(t, NULL);
  }

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  /* Checks if t has a higher priority than running thread.
     Yields if it does. */
  if (t->effective_priority > thread_current ()->effective_priority)
    thread_yield ();

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
  list_push_back (&multilevel_queue[t->effective_priority], &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
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
  if (thread_current ()->is_user)
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
  if (cur != idle_thread)
    list_push_back (&multilevel_queue[cur->effective_priority], &cur->elem);
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

void update_priorities(struct thread *t, struct lock *l) 
{
  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT (t != NULL || l != NULL);
  ASSERT (t == NULL || l == NULL);

  while (true) 
  {
    int max = t ? t->priority : PRI_MIN;
    struct list_elem *e = t ? list_begin(&t->owned_locks) : list_begin(&l->semaphore.waiters);

    for (; e != (t ? list_end(&t->owned_locks) : list_end(&l->semaphore.waiters)); e = list_next(e)) 
    {
      int pri = t ? list_entry(e, struct lock, elem)->effective_priority : list_entry(e, struct thread, elem)->effective_priority;
      if (max < pri) max = pri;
    }

    if (t) 
    {
      t->effective_priority = max;
      if (t->status == THREAD_READY) 
      {
        list_remove(&t->elem);
        list_push_back(&multilevel_queue[t->effective_priority], &t->elem);
      }
      if (!(l = t->required_lock)) break;
      t = NULL;
    } 
    else 
    {
      l->effective_priority = max;
      if (!(t = l->holder)) break;
      l = NULL;
    }
  }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  ASSERT (!thread_mlfqs);

  struct thread *t = thread_current ();
  if (t->priority != new_priority)
  {
    enum intr_level old_level = intr_disable ();
    t->priority = new_priority;
    update_priorities (t, NULL);
    intr_set_level (old_level);

    /* This operation might cause other threads to preempt. */
    if (t->effective_priority < thread_max_priority())
    {
      if (intr_context ())
        intr_yield_on_return ();
      else
        thread_yield ();
    }
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->effective_priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED)
{
  ASSERT (thread_mlfqs);

  struct thread *t = thread_current ();
  if (t->nice != nice)
  {
    enum intr_level old_level = intr_disable ();
    t->nice = nice;
    update_priority_bsd (t, NULL);
    intr_set_level (old_level);

    /* This operation might cause other threads to preempt. */
    if (t->effective_priority < thread_max_priority ())
    {
      if (intr_context ())
        intr_yield_on_return ();
      else
        thread_yield ();
    }
  }
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  ASSERT (thread_mlfqs);

  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  ASSERT (thread_mlfqs);

  return fixed_point_to_int_nearest(mult_fixed_point_by_int(load_avg, 100));
}

/* Updates the load_avg. */ 
static void 
update_load_avg (void) 
{
  ASSERT (thread_mlfqs);

  int num_ready = threads_ready ();
  if (thread_current () != idle_thread)
    num_ready++;
  fixed_point_t a = div_fixed_point_by_int (mult_fixed_point_by_int(load_avg, 59), 60);
  fixed_point_t b = div_fixed_point_by_int (int_to_fixed_point(num_ready), 60);
  load_avg = add_fixed_point(a, b);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  ASSERT (thread_mlfqs);

  return fixed_point_to_int_nearest(mult_fixed_point_by_int(thread_current()->recent_cpu, 100));
}

/* Updates the recent CPU value of thread t. */ 
static void 
update_recent_cpu (struct thread *t, void *aux UNUSED) 
{
  ASSERT (thread_mlfqs);

  fixed_point_t double_load_avg = mult_fixed_point_by_int(load_avg, 2);
  fixed_point_t x = add_int_to_fixed_point(double_load_avg, 1);
  fixed_point_t y = div_fixed_point(double_load_avg, x);
  fixed_point_t z = mult_fixed_point(y, t->recent_cpu);
  fixed_point_t a = add_int_to_fixed_point(z, t->nice);
  t->recent_cpu = a;
}

/* Updates the priority of thread t, 
   and moves it to the correct list in the multilevel feedback queue. */ 
static void
update_priority_bsd (struct thread *t, void *aux UNUSED) {

  ASSERT (thread_mlfqs);
  ASSERT (intr_get_level () == INTR_OFF);

  if (t == idle_thread) return;

  fixed_point_t a = fixed_point_to_int_nearest(div_fixed_point_by_int(t->recent_cpu, 4));
  int new_priority = PRI_MAX - a - (t->nice * 2);

  if (new_priority > PRI_MAX) {
    new_priority = PRI_MAX;
  } else if (new_priority < PRI_MIN) {
    new_priority = PRI_MIN;
  }

  int old_priority = t->priority;
  t->priority = new_priority;
  t->effective_priority = new_priority;
  if (t->status == THREAD_READY && old_priority != new_priority) {
    list_remove (&t->elem);
    list_push_back (&multilevel_queue[t->effective_priority], &t->elem);
  }
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
  t->effective_priority = priority;
  list_init (&t->owned_locks);
  t->required_lock = NULL;
  t->magic = THREAD_MAGIC;
  t->nice = 0;
  t->recent_cpu = int_to_fixed_point(0);
  
#ifdef USERPROG
  list_init(&t->open_files);
  list_init(&t->child_bonds);
  list_init(&t->mapped_files);
  t->is_user = false;
#endif

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
  for (int i = PRI_MAX; i >= PRI_MIN; i--) {
    if (!list_empty (&multilevel_queue[i])) {
      return list_entry (list_pop_front (&multilevel_queue[i]), struct thread, elem);
    }
  }
  return idle_thread;
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
