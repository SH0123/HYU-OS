#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "scheduler.h"

struct ptable ptable;
struct mlfq mlfq;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void 
setproc_default(struct proc *p)
{
  p->priority = 3;
  p->queuelevel = 0;
  p->ticks = 0;
  p->prev = 0;
  p->next = 0;
}

void
initqueue(queue *q)
{
  q->head = 0;
  q->tail = 0;
  q->count = 0;
}

int
queue_is_empty(queue *q)
{
  return q->head == 0;
}

// insert front of (base)
void 
insert(queue *q, struct proc *new, struct proc *base)
{
  if(queue_is_empty(q)) {
    q->head = new;
    new->prev = 0;
    new->next = 0;
    q->tail = new;
    q->count += 1;
  } else {
  if(base == 0) {
      // queue not empty (q) <-> new <-> 0
      q->tail->next = new;
      new->prev = q->tail;
      new->next = base;
      q->tail = new;
      q->count += 1;
    } else if(base->prev == 0) {
      // 0 <-> new <-> base
      new->next = q->head;
      q->head = new;
      base->prev = new;
      new->prev = 0;
      q->count += 1;
    } else if(base->prev != 0) {
      // (base->prev) <-> new <-> base
      new->next = base;
      base->prev->next = new;
      new->prev = base->prev;
      base->prev = new;
      q->count += 1;
    }
  }
}

void
delete(queue *q, struct proc *p)
{
  if(!queue_is_empty(q)) {
    if(q->head == p) { 
      q->head = p->next;
    }
    if(p->next != 0) {
      p->next->prev = p->prev;
    }
    if(p->prev != 0) {
      p->prev->next = p->next;
    }
    if(q->tail == p) {
      q->tail = p->prev;
    }
    p->prev = 0;
    p->next = 0;
    q->count -= 1;
  }
}

struct proc* 
dequeue(queue *q)
{
  struct proc *p;
  if(!queue_is_empty(q)) { 
    p = q->head;
    if(p->next == 0) {
      q->head = 0;
      q->tail = 0;
    } else {
      q->head = p->next;
      p->next->prev = 0;
    }
    q->count -= 1;
    p->next = 0;
    p->prev = 0;
    return p;
  } else {
    return 0;
  }
}

void
pinit(void)
{
  struct proc *p;
  initlock(&ptable.lock, "ptable");
  mlfq.lockproc = 0;
  mlfq.unlockproc = 0;

  // init queue at ptable
  for(int i = 0; i < NQUEUE ; ++i) 
    initqueue(&mlfq.l_queue[i]);
  initqueue(&mlfq.sleep_queue);
  initqueue(&mlfq.unused_queue);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; ++p){
    insert(&mlfq.unused_queue, p, 0);
  }
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
    if (cpus[i].apicid == apicid)
      return &cpus[i];
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

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  if((p = dequeue(&mlfq.unused_queue)) != 0)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  setproc_default(p);

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    insert(&mlfq.unused_queue, p, 0);
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
  return p;
}

//AGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  cprintf("%d %s priority: %d level: %d ticks: %d", p->pid, p->name, p->priority, p->queuelevel, p->ticks);
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

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  p->state = RUNNABLE;
  insert(&mlfq.l_queue[0], p, 0);

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
  int i, pid;
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
    insert(&mlfq.unused_queue, np, 0);
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

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  insert(&mlfq.l_queue[0], np, 0);
  
  release(&ptable.lock);

  return pid;
}

// release mlfq lock
void
releasemlfq()
{
  if(mlfq.lockproc != 0) {
    mlfq.lockproc = 0;
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
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

  // if some process hold lock then release it
  releasemlfq();

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
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
        setproc_default(p);
        insert(&mlfq.unused_queue, p, 0);
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

queue*
merge_queue(queue *q1, queue *q2) 
{
  if(!queue_is_empty(q1)) {
    if(!queue_is_empty(q2)) {
      q1->tail->next = q2->head;
      q2->head->prev = q1->tail;
      q1->tail = q2->tail;
      q1->count = q1->count + q2->count;
      q2->head = 0;
      q2->tail = 0;
      q2->count = 0;
    } 
    return q1;
  } else {
    if(!queue_is_empty(q2)) {
      q1->head = q2->head;
      q1->tail = q2->tail;
      q1->count += q2->count;
      q2->head = 0;
      q2->tail = 0;
      q2->count = 0;
    }
    return q1;
  }
}

void 
priority_boosting()
{
  // move concat l0, l1, l2
  // problem after boosting 
  merge_queue(&mlfq.l_queue[1], &mlfq.l_queue[2]);
  merge_queue(&mlfq.l_queue[0], &mlfq.l_queue[1]);

  // set sleep queue process queuelevel 0
  for(struct proc *p = mlfq.sleep_queue.head; p != 0; p = p->next) {
    p->queuelevel = 0;
    p->priority = 3;
    p->ticks = 0;
  }
  // // set all queue process priority, ticks default
  for(struct proc *tmp = mlfq.l_queue[0].head; tmp != 0; tmp = tmp->next) {
    tmp->queuelevel = 0;
    tmp->priority = 3;
    tmp->ticks = 0;
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    if(!queue_is_empty(&mlfq.l_queue[0])) {
      p = dequeue(&mlfq.l_queue[0]);
    } else if(!queue_is_empty(&mlfq.l_queue[1])) {
      p = dequeue(&mlfq.l_queue[1]);
    } else if(!queue_is_empty(&mlfq.l_queue[2])){
      p = dequeue(&mlfq.l_queue[2]);
    } else {
      release(&ptable.lock);
      continue;
    }
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    swtch(&(c->scheduler), p->context);
    switchkvm();
    // Process is done running for now.
    // It should have changed its p->state before coming back.

    if(mlfq.global_ticks == 100) {
      mlfq.global_ticks = 0;
      priority_boosting();
      // change queuelevel of process that held lock 
      // release lock
      releasemlfq();
    }

    c->proc = 0;
    
    release(&ptable.lock);
  }
}

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

  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// helper function for sys_schedulerUnlock: release mlfq lock
void
mlfqunlock() {
  struct proc *p = myproc();

  if(mlfq.lockproc != 0) {
    mlfq.lockproc = 0;
    mlfq.unlockproc = p;

    p->queuelevel = 0;
    p->priority = 3;
    p->ticks = 0;
  }
}

// helper function for sys_schedulerLock: hold mlfq lock
void
mlfqlock() {
  struct proc *p = myproc();

  if(mlfq.lockproc == 0) {
    mlfq.lockproc = p;
    mlfq.global_ticks = 0;
  }
}

// get process that have higher priority than p
struct proc*
higherpriority_proc(struct proc *p) {
  struct proc *tmp;
  for(tmp = mlfq.l_queue[2].head; tmp != 0; tmp = tmp->next) {
    if(tmp->priority > p->priority)
      return tmp;
  }
  return 0;
}

// helper function for sys_setPriority
void setPrioity_arrangeQueue(int pid, int priority) 
{
  struct proc *p = 0;

  acquire(&ptable.lock);

  for(struct proc *tmp = ptable.proc; tmp < &ptable.proc[NPROC]; tmp++) {
      if(tmp->pid == pid) {
        p = tmp;
      }
    }

  if(p != 0 && p->state != UNUSED) {
    p->priority = priority;
    if(p->queuelevel == 2 && p->state == RUNNABLE) {
      struct proc *base = higherpriority_proc(p);
      delete(&mlfq.l_queue[2], p);
      insert(&mlfq.l_queue[2], p, base);
    } 
  }

  release(&ptable.lock);
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&ptable.lock);   //DOC: yieldlock
  p->ticks++;
  mlfq.global_ticks++;
  myproc()->state = RUNNABLE;

  // scheduler lock
  if(mlfq.lockproc == p || mlfq.unlockproc == p) {
    if(mlfq.unlockproc == p) {
      mlfq.unlockproc = 0;
    }
    insert(&mlfq.l_queue[0], p, mlfq.l_queue[0].head);
  } else {
    // process timequantum check
    if(p->ticks == p->queuelevel*2 + 4) {
      p->ticks = 0;
      if(p->queuelevel == 2) {
        p->priority = p->priority == 0 ? 0 : p->priority - 1;
      }
      p->queuelevel = p->queuelevel == 0 ? 1 : 2;
    } 
    // process insertion
    if(p->queuelevel != 2) {
      insert(&mlfq.l_queue[p->queuelevel], p, 0);
    }
    else {
      struct proc *base = higherpriority_proc(p);
      insert(&mlfq.l_queue[2], p , base);
    }
  }

  sched();
  release(&ptable.lock);
}

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
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // if some process hold lock then release it
  releasemlfq();

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  // Go to sleep queue
  insert(&mlfq.sleep_queue, p, 0);
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p, *next = 0;

  for(p = mlfq.sleep_queue.head; p != 0; p = next) {
    next = p->next;
    if(p->chan == chan) {
      delete(&mlfq.sleep_queue, p);
      p->state = RUNNABLE;
      if(p->queuelevel == 2) {
        struct proc *base = higherpriority_proc(p);
        insert(&mlfq.l_queue[p->queuelevel], p , base);
      } else {
        insert(&mlfq.l_queue[p->queuelevel], p, 0);
      }
    }
  }
}

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
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        delete(&mlfq.sleep_queue, p);
        if(p->queuelevel == 2) {
        struct proc *base = higherpriority_proc(p);
        insert(&mlfq.l_queue[p->queuelevel], p , base);
        } else {
          insert(&mlfq.l_queue[p->queuelevel], p, 0);
        }
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// schedulerLock
void 
schedulerLock(int password)
{
  struct proc *p = myproc();

  if(password != 2017029416) {
    cprintf("현재 프로세스 pid: %d, time quantum: %d, queuelevel: %d\n", p->pid, p->ticks, p->queuelevel);
    exit();
  } else {
    acquire(&ptable.lock);
    mlfqlock();
    release(&ptable.lock);
  }
}

// schedulerUnlock
void
schedulerUnlock(int password)
{
  struct proc *p = myproc();

  if(password != 2017029416) {
    cprintf("현재 프로세스 pid: %d, time quantum: %d, queuelevel: %d\n", p->pid, p->ticks, p->queuelevel);
    exit();
  } else {
    acquire(&ptable.lock);
    mlfqunlock();
    release(&ptable.lock);
  }
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  cprintf("----mlfq0----\n");
  cprintf("해당 큐에 있는 proc의 개수는 %d개 입니다\n", mlfq.l_queue[0].count);
  if(mlfq.l_queue[0].tail != 0)
    cprintf("해당 큐의 tail값의 상태는 %s 이고 pid는 %d입니다\n", states[mlfq.l_queue[0].tail->state], mlfq.l_queue[0].tail->pid);
  for(struct proc *tmp = mlfq.l_queue[0].head; tmp != 0; tmp = tmp->next) {
    cprintf("%d %s %s %d %d\n", tmp->pid, states[tmp->state], tmp->name, tmp->queuelevel, tmp->priority);
  }
  cprintf("----mlfq1----\n");
  cprintf("해당 큐에 있는 proc의 개수는 %d개 입니다\n", mlfq.l_queue[1].count);
  if(mlfq.l_queue[1].tail != 0)
    cprintf("해당 큐의 tail값의 상태는 %s 이고 pid는 %d입니다\n", states[mlfq.l_queue[1].tail->state], mlfq.l_queue[1].tail->pid);
  for(struct proc *tmp = mlfq.l_queue[1].head; tmp != 0; tmp = tmp->next) {
    cprintf("%d %s %s %d %d\n", tmp->pid, states[tmp->state], tmp->name, tmp->queuelevel, tmp->priority);
  }
  cprintf("----mlfq2----\n");
  cprintf("해당 큐에 있는 proc의 개수는 %d개 입니다\n", mlfq.l_queue[2].count);
  if(mlfq.l_queue[2].tail != 0)
    cprintf("해당 큐의 tail값의 상태는 %s 이고 pid는 %d입니다\n", states[mlfq.l_queue[2].tail->state], mlfq.l_queue[2].tail->pid);
  for(struct proc *tmp = mlfq.l_queue[2].head; tmp != 0; tmp = tmp->next) {
    cprintf("%d %s %s %d %d\n", tmp->pid, states[tmp->state], tmp->name, tmp->queuelevel, tmp->priority);
  }
  cprintf("----unused----\n");
  cprintf("해당 큐에 있는 proc의 개수는 %d개 입니다. unused 상태가 아닌 process만 보여주므로 아무것도 보이지 않는게 정상입니다.\n", mlfq.unused_queue.count);
  for(struct proc *tmp = mlfq.unused_queue.head; tmp != 0; tmp = tmp->next) {
    if(tmp->state != UNUSED)
      cprintf("%d %s %s %d %d\n", tmp->pid, states[tmp->state], tmp->name, tmp->queuelevel, tmp->priority);
  }
  cprintf("----sleep----\n");
  cprintf("해당 큐에 있는 proc의 개수는 %d개 입니다\n", mlfq.sleep_queue.count);
  if(mlfq.sleep_queue.tail != 0)
    cprintf("해당 큐의 tail값의 상태는 %s 이고 pid는 %d입니다\n", states[mlfq.sleep_queue.tail->state], mlfq.sleep_queue.tail->pid);
  for(struct proc *tmp = mlfq.sleep_queue.head; tmp != 0; tmp = tmp->next) {
    cprintf("%d %s %s %d %d\n", tmp->pid, states[tmp->state], tmp->name, tmp->queuelevel, tmp->priority);
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    if(0) cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
