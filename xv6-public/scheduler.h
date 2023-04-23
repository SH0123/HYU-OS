typedef struct {
  struct proc *head;
  struct proc *tail;
  int count;
} queue;

struct mlfq {
  int global_ticks;
  struct proc *lockproc;
  struct proc *unlockproc;
  queue l_queue[NQUEUE];
  queue sleep_queue;
  queue unused_queue;
};

struct ptable {
  struct spinlock lock;
  struct proc proc[NPROC];
};
