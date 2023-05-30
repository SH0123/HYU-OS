#include "types.h"
#include "stat.h"
#include "user.h"

// pmanager command representation
#define LIST    1
#define KILL    2
#define EXECUTE 3
#define LIMIT   4
#define EXIT    5
#define BACK    6

struct cmd {
    int type;
};

struct listcmd {
    int type;
};

struct killcmd {
    int type;
    int pid;
};

struct executecmd {
    int type;
    char *path;
    int stacksize;
};

struct limitcmd {
    int type;
    int pid;
    int limit;
};

struct backcmd {
    int type;
    struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
void parsecmd(char*, char(*)[100]);
struct cmd* makecmd(char(*)[100]);
struct cmd* listcmd(void);
struct cmd* killcmd(int);
struct cmd* executecmd(char*, int);
struct cmd* limitcmd(int, int);
struct cmd* backcmd(struct cmd*);

// Execute cmd 
void
runcmd(struct cmd* cmd) 
{
    struct listcmd *lcmd;
    struct killcmd *kcmd;
    struct executecmd *ecmd;
    struct limitcmd *limitcmd;
    struct backcmd *bcmd;
    
    switch(cmd->type) {
    default:
        panic("runcmd");
    case LIST:
        lcmd = (struct listcmd*)cmd;
        // list print systemcall
        list();
        break;
    case KILL:
        kcmd = (struct killcmd*)cmd;
        if(kill(kcmd->pid) == 0) {
            printf(1, "kill %d success\n", kcmd->pid);
        } else {
            printf(1, "kill: fail\n");
        }
        break;
    case EXECUTE:
        ecmd = (struct executecmd*)cmd;
        // exec2 systemcall
        if(exec2(ecmd->path, (char**)ecmd->path, ecmd->stacksize) < 0) { 
            printf(1, "execute: fail\n");
        }
        break;
    case LIMIT:
        limitcmd = (struct limitcmd*)cmd;
        // setmemorylimit systemcall
        if(setmemorylimit(limitcmd->pid, limitcmd->limit) == 0) {
            printf(1, "memlim: %d process, %d stacksize limit success\n", limitcmd->pid, limitcmd->limit);
        } else {
            printf(1, "memlim: fail\n");
        }
        break;
    case BACK:
        bcmd = (struct backcmd*)cmd;
        if(fork1() == 0) {
            runcmd(bcmd->cmd);
        }
        break;
    }
    exit();
}

int
getcmd(char *buf, int nbuf)
{
    printf(2, "-> ");
    memset(buf, 0, nbuf);
    gets(buf, nbuf);
    if(buf[0] == 0) // EOF
      return -1;
    return 0;
}

int
main(void)
{
    static char buf[100];
    struct cmd* cmd;

    // Read and run input commands
    while(getcmd(buf, sizeof(buf)) >= 0) {
        char words[4][100];

        // parse commands
        parsecmd(buf, words);

        // exit command 
        if (strcmp(words[0], "exit") == 0) {
            printf(1, "exit\n");
            break;
        }

        // else -> runcmd
        cmd = makecmd(words);

        if(fork1() == 0) {
            runcmd(cmd);
        }
        wait();
    }
    exit();
}

void
parsecmd(char *input, char words[][100])
{
    char tmp[100];
    int tmpidx = 0;
    int wordcnt = 0;

    for(int i=0; i<4; ++i)
        memset(words[i], 0, 100);

    for(int i=0; i < strlen(input); ++i) {
        if(input[i] == ' ') {
            tmp[tmpidx] = '\0';
            strcpy(words[wordcnt], tmp);
            wordcnt++;
            tmpidx = 0;
        } else if(input[i] != '\n') {
            tmp[tmpidx] = input[i];
            tmpidx++;
        }
    }
    tmp[tmpidx] = '\0';
    strcpy(words[wordcnt], tmp);
}

struct cmd* makecmd(char words[][100]) 
{
    struct cmd* cmd;

    if (strcmp(words[0], "list") == 0) {
        cmd = listcmd();
    } else if(strcmp(words[0], "kill") == 0) {
        cmd = killcmd(atoi(words[1]));
    } else if(strcmp(words[0], "execute") == 0) {
        cmd = executecmd(words[1], atoi(words[2]));
    } else if(strcmp(words[0], "memlim") == 0) {
        cmd = limitcmd(atoi(words[1]), atoi(words[2]));
    } else {
        // 기타 명령어에 대한 처리
        printf(1, "cmd: command error\n");
        exit();
    }
    return cmd;
}

struct cmd* listcmd(void) 
{
    struct listcmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));

    cmd->type = LIST;
    return (struct cmd*)cmd;
}

struct cmd* killcmd(int pid) 
{  
    struct killcmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));

    cmd->type = KILL;
    cmd->pid = pid;
    return (struct cmd*)cmd;
}

struct cmd* executecmd(char* path, int stacksize)
{
    struct executecmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));

    cmd->type = EXECUTE;
    cmd->path = path;
    cmd->stacksize = stacksize;
    return backcmd((struct cmd*)cmd);
}

struct cmd* limitcmd(int pid, int limit)
{
    struct limitcmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));

    cmd->type = LIMIT;
    cmd->pid = pid;
    cmd->limit = limit;
    return (struct cmd*)cmd;
}

struct cmd* backcmd(struct cmd* subcmd)
{
    struct backcmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));

    cmd->type = BACK;
    cmd->cmd = subcmd;
    return (struct cmd*)cmd;
}

void
panic(char *s)
{
  printf(2, "%s\n", s);
  exit();
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}