// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

enum thshare_state { EMPTY, USING };

// threads sharing information
struct th_share {
  uint sz;                     // Size of process memory (bytes)
  struct proc *parent;         // Parent process
  struct file *ofile[NOFILE];  // Open files

  int memory_limit;            // Memory limit for process (including threads)
  int stacksize;
  struct proc *th_family_hd;      // Threads with same pid
  struct proc *th_last;        // Thread at last in family
  enum thshare_state state;    // Whether Using or not
};

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed

  pde_t* pgdir;                // Page table
  int pid;                     // Process ID
  char name[16];               // Process name (debugging)
  struct inode *cwd;           // Current directory
  struct th_share *shared_info;           // Threads sharing information

  thread_t tid  ;                 // Thread id
  struct proc *th_next;           // Next thread in th_family
  struct proc *th_main;       // Process that creates threads
  void *retval;                   // retval at exit
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap