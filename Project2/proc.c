#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct th_share th_share[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
void delete_thread(struct proc*);

// thread

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

// set memory limit for process who's pid matches to argument pid
// TODO: should have to handle with threads
int
setmemorylimit(int pid, int limit)
{
  struct proc *p;

  // find who's pid matches to argument pid
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid) {
      break;
    }
  }

  if(p == &ptable.proc[NPROC]) {
    return -1;
  }

  // limit is smaller than allocated process sz
  if(p->shared_info->sz > limit) {
    return -1;
  }
  cprintf("proc sz: %d, limit: %d\n", p->shared_info->sz, limit);

  p->shared_info->memory_limit = limit;

  return 0;
}

void
initialize_share(struct th_share *share)
{
  share->sz = 0;
  share->parent = 0;
  share->memory_limit = 0;
  share->th_family_hd = 0;
  share->th_last = 0;
  share->stacksize = 1;
}

static struct th_share*
alloc_th_share(void)
{
  struct th_share *share;
  for(share = ptable.th_share; share < &ptable.th_share[NPROC]; share++)
    if(share->state == EMPTY)
      goto found;

  return 0;

  found:

    initialize_share(share);

    return share;
}

void
dealloc_th_share(struct th_share *share)
{
  share->sz = 0;
  share->parent = 0;
  share->memory_limit = 0;
  share->th_family_hd = 0;
  share->th_last = 0;
  share->stacksize = 0;
  share->state = EMPTY;
}

void 
initialize_proc(struct proc *p)
{
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->tid = nexttid++;
  p->th_next = 0;
  p->th_main = p;
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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  // initialize process
  initialize_proc(p);

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

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  // thread family setting
  if((p->shared_info = alloc_th_share()) == 0)
    panic("No more thread share table entry. It's not possible");
  p->shared_info->state = USING;
  p->shared_info->th_family_hd = p;
  p->shared_info->th_last = p;
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->shared_info->sz = PGSIZE;
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

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  int limit = curproc->shared_info->memory_limit;


  sz = curproc->shared_info->sz;

  // handle memory limitation
  if(limit != 0 && (sz + n) > limit) {
    return -1;
  }

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->shared_info->sz = sz;
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
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->shared_info->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  // thread family set
  if((np->shared_info = alloc_th_share()) == 0)
      panic("No more thread share table entry. It's not possible");
  np->shared_info->state = USING;
  np->shared_info->th_family_hd = np;
  np->shared_info->th_last = np;

  np->shared_info->sz = curproc->shared_info->sz;
  np->shared_info->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->shared_info->ofile[i])
      np->shared_info->ofile[i] = filedup(curproc->shared_info->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
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
    if(curproc->shared_info->ofile[fd]){
      fileclose(curproc->shared_info->ofile[fd]);
      curproc->shared_info->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->shared_info->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->shared_info->parent == curproc){
      p->shared_info->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  // Set all proc(same pid with curproc) family state as ZOMBIE
  for(p = curproc->shared_info->th_family_hd; p != 0; p = p->th_next) {
    p->state = ZOMBIE;
  }

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  struct th_share *shared_info;
  int havekids, pid, fd, zombiekids;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    zombiekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->shared_info->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // cprintf("wait zombie?\n");
        shared_info = p->shared_info;
        zombiekids++;
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        p->pid = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->tid = 0;
        p->th_next = 0;
        p->retval = 0;
        p->th_main = 0;
        // Main thread
        if(p->th_main == p) {

          release(&ptable.lock);
          // Close all open files.
          for(fd = 0; fd < NOFILE; fd++){
            if(curproc->shared_info->ofile[fd]){
              fileclose(curproc->shared_info->ofile[fd]);
              curproc->shared_info->ofile[fd] = 0;
            }
          }
          acquire(&ptable.lock);

          // Free pgdir
          freevm(p->pgdir);
        }
      }
    }

    if(zombiekids != 0) {
      // Deallocate th_share
      dealloc_th_share(shared_info);
      release(&ptable.lock);
      return pid;
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
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

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
      c->proc = 0;
    }
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

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
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
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

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
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
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
  struct proc *tmp;
  int findproc;

  acquire(&ptable.lock);
  findproc = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      findproc = 1;
      break;
    }
  }

  // Wake process from sleep if necessary.
  if(findproc != 0) {
    for(tmp = p->shared_info->th_family_hd; tmp != 0; tmp = tmp->th_next) {
      tmp->killed = 1;
      if(tmp->state == SLEEPING)
        tmp->state = RUNNABLE;
    }
    release(&ptable.lock);
    return 0;
  }


  release(&ptable.lock);
  return -1;
}

// support function for list systemcall
void
list(void)
{
  // process sz including threads
  for(struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE || p->state == RUNNING || p->state == SLEEPING || p->state == ZOMBIE) {
      cprintf("process name: %s, pid: %d, tid: %d\n", p->name, p->pid, p->tid);
      cprintf("user stacksize: %d개, sz: %d, memlim: %d\n", p->shared_info->stacksize, p->shared_info->sz, p->shared_info->memory_limit);
    }
  }
}

// delete target thread in linked list
void 
delete_thread(struct proc* th_target)
{
  struct proc *th_tmp;
  struct proc *p;

  if(th_target->shared_info->th_family_hd == th_target) {
      // first thread
      th_target->shared_info->th_family_hd = th_target->th_next;
    } else {
      // middle, last thread
      th_tmp = th_target->shared_info->th_family_hd;
      for(p = th_target->shared_info->th_family_hd; p != 0; p = p->th_next) {
        if(p == th_target) {
          break;
        }
        th_tmp = p;
      }
      th_tmp->th_next = th_target->th_next;
      if(th_tmp->th_next == 0) {
        th_tmp->shared_info->th_last = th_tmp;
      }
    } 
}


int
thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
  uint sz, sp;
  struct proc *nthread;
  struct proc *curproc = myproc();
  struct proc* tmp;
  int mlimit;

  // deal with memory limit 
  mlimit = curproc->shared_info->memory_limit;
  if(mlimit != 0 && mlimit < curproc->shared_info->sz + 2*PGSIZE) {
    cprintf("fail: cannot allocate thread because of memory limit\n");
    return -1;
  }
  
  // create thread(proc struct) that share current process, set tid
  if((nthread = allocproc()) == 0) {
    cprintf("fail: thread allocation error\n");
    return -1;
  }

  acquire(&ptable.lock);

  // share same value ex. pgdir, pid, name, shared_info, cwd
  nthread->pgdir = curproc->pgdir;
  nthread->pid = curproc->pid;
  safestrcpy(nthread->name, curproc->name, sizeof(curproc->name));
  nthread->cwd = idup(curproc->cwd);
  nthread->shared_info = curproc->shared_info;
  nthread->th_next = 0;
  nthread->th_main = curproc;

  // allocuvm, check memory_limit for process . reference: exec
  sz = nthread->shared_info->sz;
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(nthread->pgdir, sz, sz + 2*PGSIZE)) == 0) {
    kfree(nthread->kstack);
    nthread->kstack = 0;
    nthread->state = UNUSED;
    release(&ptable.lock);
    return -1;
  }
  clearpteu(nthread->pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // add thread to thread family
  if(nthread->shared_info->th_family_hd == 0) {
    nthread->shared_info->th_family_hd = nthread;
    nthread->shared_info->th_last = nthread;
  } else {
    nthread->shared_info->th_last->th_next = nthread;
    nthread->shared_info->th_last = nthread;
  }

  // stack push argument, fake return pc . reference: exec
  sp -= 4;
  *((uint *)sp) = (uint)(arg);
  sp -= 4;
  *((uint *)sp) = 0xffffffff; // fake return PC

  // set trapframe ex. eip, esp
  nthread->shared_info->sz = sz;
  *nthread->tf = *curproc->tf;
  nthread->tf->eip = (uint)start_routine;
  nthread->tf->esp = sp;
  *thread = nthread->tid;

  // lock acquire, release and set RUNNABLE
  nthread->state = RUNNABLE;
  release(&ptable.lock);
  return 0;
}

// 언제 shared_info를 회수해줄지, 그리고 thread(process)의 데이터들을 회수할지 생각해보자
void 
thread_exit(void *retval)
{
  struct proc *cthread = myproc();

  if(cthread->th_main == cthread) {
    cprintf("fail: proc %d(tid %d) is main thread, so it cannot call thread_exit\n", cthread->pid, cthread->tid);
    return;
  }

  // deal with cwd
  begin_op();
  iput(cthread->cwd);
  end_op();
  cthread->cwd = 0;

  acquire(&ptable.lock);
  // store retval
  cthread->retval = retval;

  // wakeup parents
  wakeup1(cthread->th_main);

  // set this thread state zombie
  cthread->state = ZOMBIE;

  // sched
  sched();
  panic("zomebie exit");
}

int
thread_join(thread_t thread, void **retval)
{
  struct proc *p;
  struct proc *tmp;
  struct proc *cthread = myproc();

  acquire(&ptable.lock);

  for(;;) {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->tid != thread)
        continue;
      if(p->state == ZOMBIE) {
        *retval = p->retval;
        kfree(p->kstack);
        p->kstack = 0;
        p->pid = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->tid = 0;
        p->th_next = 0;
        p->retval = 0;
        p->th_main = 0;

        // threads family linked list set
        delete_thread(p);

        release(&ptable.lock);
        return 0;
      }
    }

    if(cthread->killed) {
      release(&ptable.lock);
      return -1;
    }
    
    sleep(cthread->th_main, &ptable.lock);
  }
}

int
kill_threads(struct proc* curproc)
{

  for(struct proc *p = curproc->shared_info->th_family_hd; p != 0; p = p->th_next) {
    if(p != curproc) {
      p->state = ZOMBIE;
    }
  }

  curproc->shared_info->th_last = curproc;
  curproc->shared_info->th_family_hd = curproc;
  curproc->th_main = curproc;
  curproc->th_next = 0;
  curproc->shared_info->memory_limit = 0;
  return 0;
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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    // if(p->state == UNUSED)
    //   continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %d %s %s", p->pid, p->tid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
