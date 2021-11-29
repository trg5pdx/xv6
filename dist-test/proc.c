#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#ifdef CS333_P2
#include "uproc.h"
#endif
#ifdef CS333_P3
#define statecount NELEM(states)
#endif // CS333_P3

static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};

#ifdef CS333_P3
// record with head and tail pointer for constant-time access to the beginning
// and end of a linked list of struct procs.  use with stateListAdd() and
// stateListRemove().
struct ptrs {
  struct proc* head;
  struct proc* tail;
};
#endif

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  #ifdef CS333_P3
  struct ptrs list[statecount];
  #endif
  #ifdef CS333_P4
  struct ptrs ready[MAXPRIO + 1];
  uint PromoteAtTime;
  #endif
} ptable;

// list management function prototypes
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void assertState(struct proc*, enum procstate, const char *, int);
#endif
// MLFQ list management function prototypes
#ifdef CS333_P4
int setpriority(int pid, int priority);
int getpriority(int pid);
#endif // CS333_P4

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
#ifdef CS333_P3
static struct proc*
allocproc(void)
{ // PROJECT 3 VERSION
  struct proc *p = NULL;
  char *sp;

  acquire(&ptable.lock);

  // Check for if there isn't any process in the list
  if(!ptable.list[UNUSED].head) {
    release(&ptable.lock);
    return 0;
  }
  if(ptable.list[UNUSED].head) {
    p = ptable.list[UNUSED].head;
    // Removing the process from the unused list
    if(stateListRemove(&ptable.list[UNUSED],p) == -1) {
      panic("ERROR: Failed to remove from unused process list!");
    }
    assertState(p, UNUSED, __FUNCTION__, __LINE__);
    // Changing its state and adding it to the embryo list
    p->state = EMBRYO;
    stateListAdd(&ptable.list[EMBRYO], p);
  }
  p->state = EMBRYO;
  p->pid = nextpid++;
  #ifdef CS333_P4
  p->priority = MAXPRIO;
  p->budget = DEFAULT_BUDGET;
  #endif // CS333_P4
  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    if(stateListRemove(&ptable.list[EMBRYO],p) == -1) {
      panic("ERROR: Failed to remove from embryo process list!");
    }
    assertState(p, EMBRYO, __FUNCTION__, __LINE__);
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->start_ticks = ticks;
  #ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  #endif // CS333_P2

  release(&ptable.lock);
  return p;
}
#else
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  int found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->start_ticks = ticks;
  #ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  #endif // CS333_P2

  return p;
}
#endif // CS333_P3

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  #ifdef CS333_P3
  acquire(&ptable.lock);
  initProcessLists();
  initFreeList();
  release(&ptable.lock);
  #endif // CS333_P3

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  #ifdef CS333_P2
  p->uid = DEF_UID;
  p->gid = DEF_GID;
  #endif // CS333_P2


  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  #ifdef CS333_P4
  if(stateListRemove(&ptable.list[EMBRYO], p) == -1) {
    panic("ERROR: Failed to remove process from Embryo list!");
  }
  assertState(p, EMBRYO, __FUNCTION__, __LINE__);
  p->state = RUNNABLE;
  stateListAdd(&ptable.ready[p->priority], p);
  ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
  #elif CS333_P3
  if(stateListRemove(&ptable.list[EMBRYO], p) == -1) {
    panic("ERROR: Failed to remove process from Embryo list!");
  }
  assertState(p, EMBRYO, __FUNCTION__, __LINE__);
  p->state = RUNNABLE;
  stateListAdd(&ptable.list[RUNNABLE], p);
  #else
  p->state = RUNNABLE;
  #endif // CS333_P3
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  #ifdef CS333_P2
  np->uid = curproc->uid;
  np->gid = curproc->gid;
  #endif // CS333_P2

  acquire(&ptable.lock);
  #ifdef CS333_P4
  if(stateListRemove(&ptable.list[EMBRYO], np) == -1) {
    panic("ERROR: Failed to remove process from Embryo list!");
  }
  assertState(np, EMBRYO, __FUNCTION__, __LINE__);
  np->state = RUNNABLE;
  stateListAdd(&ptable.ready[np->priority], np);
  #elif CS333_P3
  // Removing the process from the Embryo list, adding to runnable list
  if(stateListRemove(&ptable.list[EMBRYO], np) == -1) {
    panic("ERROR: Failed to remove process from Embryo list!");
  }
  assertState(np, EMBRYO, __FUNCTION__, __LINE__);
  np->state = RUNNABLE;
  stateListAdd(&ptable.list[RUNNABLE], np);
  #else
  np->state = RUNNABLE;
  #endif // CS333_P3
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifdef CS333_P3
void
exit(void)
{ // PROJECT 3 VERSION
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  #ifdef CS333_P4
  for(int i = 0; i <= MAXPRIO; ++i) {
    p = ptable.ready[i].head;
    while(p) {
      if(p->parent == curproc) {
        p->parent = initproc;
      }
      p = p->next;
    }
  }
  #endif // CS333_P4

  // Pass abandoned children to init.
  for(enum procstate i = EMBRYO; i <= ZOMBIE; ++i) {
    p = ptable.list[i].head;
    while(p) {
      if(p->parent == curproc) {
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
      }
      p = p->next;
    }
  }

  // Jump into the scheduler, never to return.
  // Removing curproc from Running and putting it into the zombie list
  if(stateListRemove(&ptable.list[RUNNING], curproc) == -1) {
    panic("ERROR: Failed to remove process from running list!");
  }
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = ZOMBIE;
  stateListAdd(&ptable.list[ZOMBIE], curproc);
  #ifdef PDX_XV6
  curproc->sz = 0;
  #endif // PDX_XV6
  sched();
  panic("zombie exit");

}
#else
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#endif // CS333_P3

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P3
int
wait(void)
{   // PROJECT 3 VERSION
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    #ifdef CS333_P4
    for(int i = 0; i <= MAXPRIO; ++i) {
      p = ptable.ready[i].head;
      while(p) {
        if(p->parent == curproc) {
          havekids = 1;
        }
        p = p->next;
      }
    }
    #endif // CS333_P4
    for(int i = EMBRYO; i <= ZOMBIE; ++i){
      p = ptable.list[i].head;
      while(p) {
        if(p->parent != curproc) {
          p = p->next;
          continue;
        }
        havekids = 1;
        if(p->state == ZOMBIE){
          // Found one.
          pid = p->pid;
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          if(stateListRemove(&ptable.list[ZOMBIE], p) == -1) {
            panic("ERROR: Failed to remove process from zombie list!");
          }
          assertState(p, ZOMBIE, __FUNCTION__, __LINE__);
          p->state = UNUSED;
          stateListAdd(&ptable.list[UNUSED], p);
          release(&ptable.lock);
          return pid;
        }
        p = p->next;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

#else
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#endif // CS333_P3

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifdef CS333_P4
void
scheduler(void)
{ // PROJECT 4 VERSION
  struct proc *p, *current;
  struct cpu *c = mycpu();
  int i = MAXPRIO;
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.

    acquire(&ptable.lock);
    for(i = MAXPRIO; i >= 0; --i) {
      p = ptable.ready[i].head;
      if(p) {
        break;
      }
    }

    if(ticks >= ptable.PromoteAtTime) {
      for(int i = SLEEPING; i < RUNNING; ++i) {
        p = ptable.list[i].head;
        while(p) {
          if(p->priority < MAXPRIO) {
            ++p->priority;
            p->budget = DEFAULT_BUDGET;
          }
          p = p->next;
        }
      }
      for(int i = MAXPRIO - 1; i >= 0; --i) {
        p = ptable.ready[i].head;
        while(p) {
          current = p->next;
          ++p->priority;
          p->budget = DEFAULT_BUDGET;
          if(stateListRemove(&ptable.ready[i], p) == -1) {
            panic("ERROR: failed to remove process from runnable list!");
          }
          stateListAdd(&ptable.ready[p->priority], p);
          p = current;
        }
      }
      ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
    }

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    if(p) {
      #ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
      #endif // PDX_XV6
      c->proc = p;
      switchuvm(p);

      if(stateListRemove(&ptable.ready[i], p) == -1) {
        panic("ERROR: failed to remove process from runnable list!");
      }
      assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
      p->state = RUNNING;
      stateListAdd(&ptable.list[RUNNING], p);

      #ifdef CS333_P2
      p->cpu_ticks_in = ticks;
      #endif // CS333_P2

      swtch(&(c->scheduler), p->context); switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#elif CS333_P3
void
scheduler(void)
{ // PROJECT 3 VERSION
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    /*
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue; */

    p = ptable.list[RUNNABLE].head;
    while(p) {
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);

      if(stateListRemove(&ptable.list[RUNNABLE], p) == -1) {
        panic("ERROR: failed to remove process from running list!");
      }
      assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
      p->state = RUNNING;
      stateListAdd(&ptable.list[RUNNING], p);

      #ifdef CS333_P2
      p->cpu_ticks_in = ticks;
      #endif // CS333_P2
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;

      p = p->next;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#else
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      #ifdef CS333_P2
      p->cpu_ticks_in = ticks;
      #endif // CS333_P2
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}

#endif // CS333_P3

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  #ifdef CS333_P2
  p->cpu_ticks_total += ticks - p->cpu_ticks_in;
  #endif // CS333_P2
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
#ifdef CS333_P4
void
yield(void)
{ // PROJECT 4 VERSION
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  if(stateListRemove(&ptable.list[RUNNING], curproc) == -1) {
    panic("ERROR: Failed to remove process from running list!");
  }
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = RUNNABLE;

  curproc->budget = curproc->budget - (ticks - curproc->cpu_ticks_in);
  if(curproc->budget <= 0 && curproc->priority > 0) {
    --curproc->priority;
    curproc->budget = DEFAULT_BUDGET;
  }

  stateListAdd(&ptable.ready[curproc->priority], curproc);
  sched();
  release(&ptable.lock);
}
#elif CS333_P3
void
yield(void)
{ // PROJECT 3 VERSION
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  if(stateListRemove(&ptable.list[RUNNING], curproc) == -1) {
    panic("ERROR: Failed to remove process from running list!");
  }
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = RUNNABLE;
  stateListAdd(&ptable.list[RUNNABLE], curproc);
  sched();
  release(&ptable.lock);
}
#else
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
#endif // CS333_P3


// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
#ifdef CS333_P3
void
sleep(void *chan, struct spinlock *lk)
{ // PROJECT 3 VERSION
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  if(stateListRemove(&ptable.list[RUNNING], p) == -1) {
    panic("ERROR: Failed to remove process from running list!");
  }
  assertState(p, RUNNING, __FUNCTION__, __LINE__);
  p->state = SLEEPING;
  stateListAdd(&ptable.list[SLEEPING],p);

  #ifdef CS333_P4
  p->budget = p->budget - (ticks - p->cpu_ticks_in);
  if(p->budget <= 0 && p->priority > 0) {
    --p->priority;
    p->budget = DEFAULT_BUDGET;
  }
  #endif // CS333_P4

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#else
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#endif // CS333_P3

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifdef CS333_P4
static void
wakeup1(void *chan)
{ // PROJECT 4 VERSION
  struct proc *p = ptable.list[SLEEPING].head;
  struct proc *t = NULL;

  while(p) {
    t = p->next;
    if(p->chan == chan) {
      if(stateListRemove(&ptable.list[SLEEPING], p) == -1) {
        panic("ERROR: failed to remove process from sleeping list!");
      }
      assertState(p, SLEEPING, __FUNCTION__, __LINE__);
      p->state = RUNNABLE;
      stateListAdd(&ptable.ready[p->priority], p);
    }
    p = t;
  }
}
#elif CS333_P3
static void
wakeup1(void *chan)
{ // PROJECT 3 VERSION
  struct proc *p = ptable.list[SLEEPING].head;
  struct proc *t = NULL;

  while(p) {
    t = p->next;
    if(p->chan == chan) {
      if(stateListRemove(&ptable.list[SLEEPING], p) == -1) {
        panic("ERROR: failed to remove process from sleeping list!");
      }
      assertState(p, SLEEPING, __FUNCTION__, __LINE__);
      p->state = RUNNABLE;
      stateListAdd(&ptable.list[RUNNABLE], p);
    }
    p = t;
  }
}
#else
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#endif // CS333_P3

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
#ifdef CS333_P4
int
kill(int pid)
{ // PROJECT 4 VERSION
  struct proc *p;

  acquire(&ptable.lock);
  for(enum procstate i = EMBRYO; i <= ZOMBIE; ++i) {
    p = ptable.list[i].head;
    while(p) {
      if(p->pid == pid) {
        p->killed = 1;
        if(i == SLEEPING) {
          if(stateListRemove(&ptable.list[i], p) == -1) {
            panic("ERROR: failed to remove process from sleeping list!");
          }
          assertState(p, i, __FUNCTION__, __LINE__);
          p->state = RUNNABLE;
          stateListAdd(&ptable.ready[p->priority], p);
        }
        release(&ptable.lock);
        return 0;
      }
      p = p->next;
    }
  }
  for(int i = 0; i <= MAXPRIO; ++i) {
    p = ptable.ready[i].head;
    while(p) {
      if(p->pid == pid) {
        p->killed = 1;
        if(stateListRemove(&ptable.ready[p->priority], p) == -1) {
          panic("ERROR: failed to remove process from sleeping list!");
        }
        assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
        p->state = RUNNABLE;
        p->priority = MAXPRIO;
        stateListAdd(&ptable.ready[p->priority], p);
        release(&ptable.lock);
        return 0;
      }
      p = p->next;
    }
  }

  release(&ptable.lock);
  return -1;
}
#elif CS333_P3
int
kill(int pid)
{ // PROJECT 3 VERSION
  struct proc *p;

  acquire(&ptable.lock);
  for(enum procstate i = EMBRYO; i <= ZOMBIE; ++i) {
    p = ptable.list[i].head;
    while(p) {
      if(p->pid == pid) {
        p->killed = 1;
        if(i == SLEEPING) {
          if(stateListRemove(&ptable.list[i], p) == -1) {
            panic("ERROR: failed to remove process from sleeping list!");
          }
          assertState(p, i, __FUNCTION__, __LINE__);
          p->state = RUNNABLE;
          stateListAdd(&ptable.list[RUNNABLE], p);
        }
        release(&ptable.lock);
        return 0;
      }
      p = p->next;
    }
  }

  release(&ptable.lock);
  return -1;
}
#else
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
#endif // CS333_P3

#ifdef CS333_P2
int
grabprocs(struct uproc * up, int processNum)
{
  struct proc * p;
  int i = 0;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p != NULL) {
        if(p->state != EMBRYO || p->state != UNUSED) {
          up[i].pid = p->pid;
          up[i].uid = p->uid;
          up[i].gid = p->gid;
          up[i].priority = p->priority;

          if(p->parent == NULL)
            up[i].ppid = p->pid;
          else
            up[i].ppid = p->parent->pid;

          up[i].elapsed_ticks = p->cpu_ticks_in;
          up[i].CPU_total_ticks = p->cpu_ticks_total;
          up[i].size = p->sz;
          safestrcpy(up[i].name, p->name, sizeof(p->name));
          safestrcpy(up[i].state, states[p->state], sizeof(up[i].state));
          ++i;
        }
      }
      if(i >= processNum)
        break;
  }

  release(&ptable.lock);

  return i;
}

#endif // CS333_P2

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

#if defined(CS333_P4)
void
procdumpP4(struct proc *p, char *state_string)
{
  uint uptime = ticks - p->start_ticks;
  uint uptime_milliseconds = uptime % 1000;
  uint uptime_seconds = (uptime - uptime_milliseconds)/1000;
  uint ppid;

  uint cpu_ticksMS = p->cpu_ticks_total % 1000;
  uint cpu_ticksS = (p->cpu_ticks_total - cpu_ticksMS)/1000;


  if(!p->parent)
    ppid = p->pid;
  else
    ppid = p->parent->pid;

  cprintf("%d\t%s\t\t%d\t%d\t%d\t%d\t%d.%d\t%d.%d\t%s\t%d\t", p->pid, p->name, p->uid, p->gid, ppid, p->priority, uptime_seconds, uptime_milliseconds, cpu_ticksS, cpu_ticksMS, state_string, p->sz);

  return;
}
#elif defined(CS333_P3)
void
procdumpP3(struct proc *p, char *state_string)
{
  uint uptime = ticks - p->start_ticks;
  uint uptime_milliseconds = uptime % 1000;
  uint uptime_seconds = (uptime - uptime_milliseconds)/1000;
  uint ppid;

  uint cpu_ticksMS = p->cpu_ticks_total % 1000;
  uint cpu_ticksS = (p->cpu_ticks_total - cpu_ticksMS)/1000;


  if(!p->parent)
    ppid = p->pid;
  else
    ppid = p->parent->pid;

  cprintf("%d\t%s\t\t%d\t%d\t%d\t%d.%d\t%d.%d\t%s\t%d\t", p->pid, p->name, p->uid, p->gid, ppid, uptime_seconds, uptime_milliseconds, cpu_ticksS, cpu_ticksMS, state_string, p->sz);

  return;

}
#elif defined(CS333_P2)
void
procdumpP2(struct proc *p, char *state_string)
{
  uint uptime = ticks - p->start_ticks;
  uint uptime_milliseconds = uptime % 1000;
  uint uptime_seconds = (uptime - uptime_milliseconds)/1000;
  uint ppid;

  uint cpu_ticksMS = p->cpu_ticks_total % 1000;
  uint cpu_ticksS = (p->cpu_ticks_total - cpu_ticksMS)/1000;


  if(!p->parent)
    ppid = p->pid;
  else
    ppid = p->parent->pid;

  cprintf("%d\t%s\t\t%d\t%d\t%d\t%d.%d\t%d.%d\t%s\t%d\t", p->pid, p->name, p->uid, p->gid, ppid, uptime_seconds, uptime_milliseconds, cpu_ticksS, cpu_ticksMS, state_string, p->sz);

  return;
}
#elif defined(CS333_P1)
void
procdumpP1(struct proc *p, char *state_string)
{
  uint uptime = ticks - p->start_ticks;
  uint uptime_milliseconds = uptime % 1000;
  uint uptime_seconds = (uptime - uptime_milliseconds)/1000;

  cprintf("%d\t%s\t\t%d.%d\t%s\t%d\t", p->pid, p->name, uptime_seconds, uptime_milliseconds, state_string, p->sz);

  return;
}
#endif

void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

#if defined(CS333_P4)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P3)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName         Elapsed\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif

  cprintf(HEADER);  // not conditionally compiled as must work in all project states

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    // see TODOs above this function
#if defined(CS333_P4)
    procdumpP4(p, state);
#elif defined(CS333_P3)
    procdumpP3(p, state);
#elif defined(CS333_P2)
    procdumpP2(p, state);
#elif defined(CS333_P1)
    procdumpP1(p, state);
#else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif

    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
#ifdef CS333_P1
  cprintf("$ ");  // simulate shell prompt
#endif // CS333_P1
}

#ifdef CS333_P4
void
rundump(void)
{
  struct proc* p = NULL;
  acquire(&ptable.lock);

  cprintf("Ready List Processes: \n");
  for(int i = MAXPRIO; i >= 0; --i) {
    cprintf("%d: ", i);
    p = ptable.ready[i].head;
    while(p) {
      if(p != ptable.ready[i].tail)
        cprintf("(%d, %d) -> ", p->pid, p->budget);
      else
        cprintf("(%d, %d) \n", p->pid, p->budget);
      p = p->next;
    }
    if(!p)
      cprintf("\n");
  }

  release(&ptable.lock);
  return;
}
#elif CS333_P3
// These functions are meant to be used for testing the functionality of the new process management system
// rundump is meant for displaying the runnable processes
void
rundump(void)
{
  struct proc* p = NULL;
  acquire(&ptable.lock);
  p = ptable.list[RUNNABLE].head;

  cprintf("Ready List Processes: \n");
  while(p) {
    // Checking if its at the end of the list so the data gets formatted correctly
    if(p != ptable.list[RUNNABLE].tail)
      cprintf("%d -> ", p->pid);
    else
      cprintf("%d \n", p->pid);
    p = p->next;
  }
  release(&ptable.lock);
  return;
}
#endif // CS333_P4/P3

#ifdef CS333_P3
// unusedump is meant for displaying the unused processes
void
unusedump(void)
{
  struct proc * current = NULL;
  int counter = 0;
  acquire(&ptable.lock);
  current = ptable.list[UNUSED].head;
  while(current) {
    current = current->next;
    ++counter;
  }
  release(&ptable.lock);

  cprintf("Free List Size: %d processes\n", counter);
  return;
}
// sleepdump is meant for displaying the sleeping processes
void
sleepdump(void)
{
  struct proc* p = NULL;
  acquire(&ptable.lock);
  p = ptable.list[SLEEPING].head;

  cprintf("Sleep List Processes: \n");
  while(p) {
    // Checking if its at the end of the list so the data gets formatted correctly
    if(p != ptable.list[SLEEPING].tail)
      cprintf("%d -> ", p->pid);
    else
      cprintf("%d \n", p->pid);
    p = p->next;
  }
  release(&ptable.lock);

  return;
}
// zombdump is meant for displaying the zombie processes
void
zombdump(void)
{
  struct proc* p = NULL;
  acquire(&ptable.lock);
  p = ptable.list[ZOMBIE].head;

  cprintf("Zombie List Processes: \n");
  while(p) {
    // Checking to see if p is at the tail for formatting reasons
    if(p != ptable.list[ZOMBIE].tail) {
      // Checking to see if the parent exists to see what pid to print
      if(p->parent) {
        cprintf("(%d, %d) -> ", p->pid, p->parent->pid);
      }
      else
        cprintf("(%d, %d) -> ", p->pid, p->pid);
    }
    else {
      // Checking to see if the parent exists to see what pid to print
      if(p->parent) {
        cprintf(" (%d, %d)", p->pid, p->parent->pid);
      }
      else
        cprintf(" (%d, %d)", p->pid, p->pid);
    }
    p = p->next;
  }
  cprintf("\n");

  release(&ptable.lock);
  return;
}

#endif // CS333_P3

#if defined(CS333_P3)
// list management helper functions
static void
stateListAdd(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL){
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  } else{
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}
#endif

#if defined(CS333_P3)
static int
stateListRemove(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL || (*list).tail == NULL || p == NULL){
    return -1;
  }

  struct proc* current = (*list).head;
  struct proc* previous = 0;

  if(current == p){
    (*list).head = ((*list).head)->next;
    // prevent tail remaining assigned when we've removed the only item
    // on the list
    if((*list).tail == p){
      (*list).tail = NULL;
    }
    return 0;
  }

  while(current){
    if(current == p){
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found. return error
  if(current == NULL){
    return -1;
  }

  // Process found.
  if(current == (*list).tail){
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  } else{
    previous->next = current->next;
  }

  // Make sure p->next doesn't point into the list.
  p->next = NULL;

  return 0;
}
#endif

#if defined(CS333_P3)
static void
initProcessLists()
{
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++) {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
  #if defined(CS333_P4)
  for (i = 0; i <= MAXPRIO; i++) {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
  #endif
}
#endif

#if defined(CS333_P3)
static void
initFreeList(void)
{
  struct proc* p;

  for(p = ptable.proc; p < ptable.proc + NPROC; ++p){
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}
#endif

#if defined(CS333_P3)
// example usage:
// assertState(p, UNUSED, __FUNCTION__, __LINE__);
// This code uses gcc preprocessor directives. For details, see
// https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html
static void
assertState(struct proc *p, enum procstate state, const char * func, int line)
{
    if (p->state == state)
      return;
    cprintf("Error: proc state is %s and should be %s.\nCalled from %s line %d\n",
        states[p->state], states[state], func, line);
    panic("Error: Process state incorrect in assertState()");
}
#endif

#ifdef CS333_P4
int
setpriority(int pid, int priority) {
  struct proc * p = NULL;
  int foundProc = 0;

  if(priority < 0 || priority > MAXPRIO) {
    return -1;
  }
  if(pid < 0) {
    return -1;
  }

  acquire(&ptable.lock);
  for(int i = 0; i < MAXPRIO + 1; ++i) {
    p = ptable.ready[i].head;
    while(p) {
      if(pid == p->pid) {
        foundProc = 1;
        break;
      }
      p = p->next;
    }
    if(foundProc == 1)
      break;
  }

  // This works since the old runnable list is NULL
  for(int i = SLEEPING; i < ZOMBIE; ++i) {
    p = ptable.list[i].head;
    while(p) {
      if(pid == p->pid) {
        foundProc = 1;
        break;
      }
      p = p->next;
    }
    if(foundProc == 1)
      break;
  }

  if(foundProc == 0) {
    release(&ptable.lock);
    return -1;
  }

  if(p->priority != priority) {
    p->priority = priority;
    p->budget = DEFAULT_BUDGET;
    if(p->state == RUNNABLE) {
      if(stateListRemove(&ptable.ready[p->priority], p) == -1) {
        panic("ERROR: failed to remove process inside of setpriority function!\n");
      }
      stateListAdd(&ptable.ready[priority], p);
    }
  }
  release(&ptable.lock);
  return 0;
}

int
getpriority(int pid) {
  struct proc * p = NULL;
  int foundProc = 0;
  int rv;

  if(pid < 0) {
    return -1;
  }

  acquire(&ptable.lock);
  // Looping through the ready priority list to find process
  for(int i = 0; i < MAXPRIO + 1; ++i) {
    p = ptable.ready[i].head;
    while(p) {
      if(pid == p->pid) {
        foundProc = 1;
        break;
      }
      p = p->next;
    }
    if(foundProc == 1)
      break;
  }
  // If process isn't already found, now looking in other state lists
  if(foundProc == 0) {
    for(int i = UNUSED; i <= ZOMBIE; ++i) {
      p = ptable.list[i].head;
      while(p) {
        if(pid == p->pid) {
          foundProc = 1;
          break;
        }
        p = p->next;
      }
      if(foundProc == 1)
        break;
    }
  }

  if(foundProc == 0) {
    release(&ptable.lock);
    return -1;
  }

  rv = p->priority;
  release(&ptable.lock);
  return rv;
}
#endif // CS333_P4

