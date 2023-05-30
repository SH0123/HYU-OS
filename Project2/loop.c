#include "types.h"
#include "stat.h"
#include "user.h"

#define NULL ((void*)0)
#define THREADNUM   5

void* hello(void *arg)
{
  printf(1, "hello\n");
  thread_exit((void*) NULL);
}

int k = 100;
void* loop(void* num)
{   
  thread_t thread6;
  int i = 6;
  char *retval[5];
  retval[0] = "d";
  static char *args[5];
  args[0] = "hihihihihi";
  args[1] = "ddddddd";

  if(*(int*)num == 1) {
  // thread_create(&thread6, hello, &i);
  // thread_join(thread6, (void**)retval);
    exit();
  }
  while(k-- > 0) { 
    printf(1, "%d\n", k);
  }
  thread_exit((void*)NULL);
  return NULL;
}

int maidn(int argc, char* argv[])
{
   int i = 1;
    int j = 2;
    int a = 3;
    int b = 4;
    int c = 5;
    thread_t thread1;
    thread_t thread2;
    thread_t thread3;
    thread_t thread4;
    thread_t thread5;

    char* retVal[5];
    retVal[0] = "ls";
    
    thread_create(&thread1, loop, &i);
    thread_create(&thread2, loop, &j);
    thread_create(&thread3, loop, &a); 
    thread_create(&thread4, loop, &b);
    thread_create(&thread5, loop, &c);
    
    thread_join(thread1, (void**)retVal);
    thread_join(thread2, (void**)retVal);
    thread_join(thread3, (void**)retVal);
    thread_join(thread4, (void**)retVal);
    thread_join(thread5, (void**)retVal);
    
    exit();
}

void*
execthreadmain(void *arg)
{
  char *args[3] = {"echo", "echo is executed!", 0}; 
  printf(1, "execute start\n");
  exec("echo", args);

  printf(1, "panic at exec thread main\n");
  thread_exit((void*)NULL);
  exit();
}


int
main(int argc, char* argv[])
{
  thread_t threads[THREADNUM];
  int i;
  void *retval;

  for (i = 0; i < THREADNUM; i++){
    if (thread_create(&threads[i], execthreadmain, (void*)0) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for (i = 0; i < THREADNUM; i++){
    if (thread_join(threads[i], &retval) != 0){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  printf(1, "panic at exectest\n");
  return 0;
}