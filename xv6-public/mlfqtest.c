#include "types.h"
#include "stat.h"
#include "user.h"

// set priority of pid 1 as 0
// initial: 3 -> set: 0
void setpriority_test() {
    setPriority(1, 0);
}

void scheduler_lock_test() {
    schedulerLock(2017029416);
    int pid = fork();
    if(pid == 0) {
        while(1) {
            printf(1, "child is called\n");
        }
    } else {
        while(1) {
            printf(1, "parent is called\n");
        }
    }
}

void scheduler_lock_unlock_test() {
    schedulerLock(2017029416);
    int pid = fork();
    if(pid == 0) {
        printf(1, "child is called\n");
    } else {
        printf(1, "parent is called\n");
        schedulerUnlock(2017029416);
        wait();
    }
}

void scheduler_lock_and_sleep_test() {
    schedulerLock(2017029416);
    int pid = fork();
    if(pid == 0) {
        printf(1, "child is called\n");
    } else {
        sleep(100);
        printf(1, "parent is called\n");
        exit();
    }
}

void scheduler_lock_and_exit_test() {
    schedulerLock(2017029416);
    int pid = fork();
    if(pid == 0) {
        printf(1, "child is called\n");
    } else {
        exit();
        printf(1, "parent is called\n");
    }
}

int
main(int argc, char* argv[])
{   
    scheduler_lock_test();
    exit();
}