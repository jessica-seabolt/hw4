// Message queue code adapted from: https://www.geeksforgeeks.org/ipc-using-message-queues/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <time.h>


#define _GNU_SOURCE
#define MAX_PROCESSES 20

struct PCB {
  int occupied; // either true or false
  pid_t pid; // process id of this child
  int startSeconds; // time when it was created
  int startNano; // time when it was created
  int serviceTimeSeconds; // total seconds it has been "scheduled"
  int serviceTimeNano; // total nanoseconds it has been "scheduled"
  int eventWaitSec; // when does its event happen?
  int eventWaitNano; // when does its event happen?
  int blocked; // is this process waiting on event?
};

struct PCB processTable[MAX_PROCESSES];

struct SystemClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

// Setup shared memory for SystemClock
int shmid;
struct SystemClock *systemClock;

// Message queue struct
struct msgbuf {
  long mType;
  int mNum;
};

// Initialize shared memory segment
void init_shm() {
  shmid = shmget(0x1234, sizeof(struct SystemClock), 0666 | IPC_CREAT);
  if (shmid == -1) {
    perror("shmget");
    exit(1);
  }

  systemClock = shmat(shmid, NULL, 0);
  if (systemClock == (void *) -1) {
    perror("shmat");
    exit(1);
  }
}

void write_log(const char* logfile, const char* message) {
  int fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd < 0) {
    perror("open");
    return;
  }

  dprintf(fd, "%s\n", message);
  close(fd);
}

void advanceClock(unsigned int incrementNanos) {
  // Add the increment to the current clock time
  systemClock->nanoseconds += incrementNanos;

  // Handle the overflow of nanoseconds to seconds
  while (systemClock->nanoseconds >= 1000000000) {
    systemClock->nanoseconds -= 1000000000;
    systemClock->seconds += 1;
  }
}

// Function to find a free PCB entry
int find_free_pcb() {
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (!processTable[i].occupied) {
      return i;
    }
  }
  return -1; // No free entry
}

// Function to handle signals
void handleSignal(int sig) {
  if (sig == SIGINT) {
    char* exitMessage = ("OSS: Caught SIGINT, exiting...\n");
  } else if (sig == SIGALRM) {
    char* exitMessage = ("OSS: Caught SIGALRM, exiting...\n");
  }
  write_log(logfile, exitMessage);
  printf("%s", exitMessage);
  shmctl(shmid, IPC_RMID, NULL);
  msgctl(msgget(ftok("oss.c", 1), 0666 | IPC_CREAT), IPC_RMID, NULL);
  exit(0);
}

int main(int argc, char *argv[]) {
  int n = -1; // number of processes
  int s = -1; // max simultaneous processes 
  int t = -1; // time interval
  char* logfile = NULL;

  int opt;
  while ((opt = getopt(argc, argv, "hn:s:t:f:")) != -1) {
    switch(opt) {
      case 'h':
        printf("Usage: %s [-h] [-n num procs] [-s max simul procs] [-t time in nanoseconds] [-f logfile]\n", argv[0]);
        exit(0);
      case 'n':
        n = atoi(optarg);
        break;
      case 's':
        s = atoi(optarg);
        break;
      case 't':
        t = atoi(optarg);
        break;
      case 'f':
        logfile = optarg;
        break;
    }
  }

  // Setup signal handlers
  signal(SIGINT, handleSignal);
  signal(SIGALRM, handleSignal);
  alarm(3); // Set alarm for 3 seconds

  // Handle missing arguments
  if (n == -1 || s == -1 || t == -1) {
    fprintf(stderr, "Error: Missing required arguments.\n");
    exit(1);
  } 

  // Handle invalid arguments
  if (!logfile) {
    fprintf(stderr, "Error: No logfile specified.\n");
    exit(1);
  } else if (n < 1 || s < 1 || s > 20 || t < 1) {
    fprintf(stderr, "Error: Invalid arguments.\n");
    exit(1);
  }
  

  // Initialize shared memory segment
  init_shm();

  // Initialize process table
  for (int i = 0; i < MAX_PROCESSES; i++) {
    processTable[i].occupied = 0;
  }

  // Initialize system clock to 0
  systemClock->seconds = 0;
  systemClock->nanoseconds = 0;

  // Setup process variables
  int total_processes_launched = 0;
  int active_processes = 0;
  unsigned int nextLaunchTimeNano = 0; // Launch processes after 't' nanoseconds

  srand(time(NULL)); // Seed for random number generation

  // Setup message queue
  struct msgbuf inbox, outbox;
  key_t msgKey = ftok("oss.c", 1);
  if (msgKey == -1) {
    perror("ftok");
    exit(1);
  }

  int msgQid = msgget(msgKey, 0666 | IPC_CREAT); 

  while (total_processes_launched < n || active_processes > 0) {

    // Check if it's time to launch a new process
    if (systemClock->nanoseconds >= nextLaunchTimeNano) {
      if (active_processes < s && total_processes_launched < n) {
        // Simulate scheduling time taken by OSS
        unsigned int schedulingTime = 100 + rand() % 9900; // Random time between 100 to 10000 nanoseconds
        advanceClock(schedulingTime);

        // Launch new process
        int pcb_index = find_free_pcb(); // TODO: Add error handling for no free PCB
        
        pid_t pid = fork();
        if (pid == 0) {
          // Child process code
          execl("./worker", "worker", NULL);
          exit(0);
        } else if (pid > 0) {
          // Parent process code
          processTable[pcb_index].occupied = 1;
          processTable[pcb_index].pid = pid;
          processTable[pcb_index].startSeconds = systemClock->seconds;
          processTable[pcb_index].startNano = systemClock->nanoseconds;
          processTable[pcb_index].serviceTimeSeconds = 0;
          processTable[pcb_index].serviceTimeNano = 0;
          processTable[pcb_index].eventWaitSec = 0;
          processTable[pcb_index].eventWaitNano = 0;
          processTable[pcb_index].blocked = 0;
        } else {
          // Handle error: fork() failed
          perror("fork() failed");
          // Clean up shared memory, message queue, and any other resources used
          handleSignal(SIGINT);
        }

        total_processes_launched++;
        active_processes++;

        // Calculate next launch time
        nextLaunchTimeNano += t;
      }
    }

    int totalTime = systemClock->seconds * 1000000000 + systemClock->nanoseconds;
    double priority = 0;
    double lowestPriority = 2;
    int pcb_index = -1;

    // Calculate priority of each process
    for (int i = 0; i < MAX_PROCESSES; i++) {
      if (processTable[i].occupied && !processTable[i].blocked) {
        int serviceTime = processTable[i].serviceTimeSeconds * 1000000000 + processTable[i].serviceTimeNano;
        int processTime = totalTime - (processTable[i].startSeconds * 1000000000 + processTable[i].startNano);
        if (processTime <= 0) {
          priority = 0;
        } else {
          priority = serviceTime / processTime;
      }
        if (priority < lowestPriority) {
          lowestPriority = priority;
          pcb_index = i;
        }
      }
    }
    advanceClock(1000); // Simulate scheduling time taken by OSS

    // Compare blocked processes to current time
    for (int i = 0; i < MAX_PROCESSES; i++) {
      if (processTable[i].occupied && processTable[i].blocked) {
        int eventTime = processTable[i].eventWaitSec * 1000000000 + processTable[i].eventWaitNano;
        if (eventTime <= totalTime) {
          // Unblock process
          processTable[i].blocked = 0;
          processTable[i].eventWaitSec = 0;
          processTable[i].eventWaitNano = 0;
          unsigned int movingTime = 200 + rand() % 19800; // Additional time to move out of blocked state
          advanceClock(movingTime);
        }  
      }
    }
    

    // Message queue code
    if (active_processes > 0 && pcb_index != -1) {
      // Send message to an active child process
      outbox.mNum = 50000000;
      outbox.mType = processTable[pcb_index].pid;
      printf("OSS: Sending message to child process %d\n", processTable[pcb_index].pid);
      msgsnd(msgQid, &outbox, sizeof(outbox), 0);
      advanceClock(1000); // Simulate scheduling time taken by OSS

      // Wait for message from child process
      printf("OSS: Waiting for a message...\n");
      msgrcv(msgQid, &inbox, sizeof(inbox), 1, WNOHANG);
      if (inbox.mNum < 0) {
        // Child process has terminated
        printf("OSS: Child process %d has terminated\n", processTable[pcb_index].pid);
        active_processes--;
        processTable[pcb_index].occupied = 0;
        advanceClock(inbox.mNum * -1);
      } else if (inbox.mNum < 50000000) {
        // Child process has requested to be blocked
        printf("OSS: Child process %d has requested to be blocked\n", processTable[pcb_index].pid);
        processTable[pcb_index].serviceTimeNano += inbox.mNum;
        if (processTable[pcb_index].serviceTimeNano >= 1000000000) {
          processTable[pcb_index].serviceTimeNano -= 1000000000;
          processTable[pcb_index].serviceTimeSeconds += 1;
        }

        int r = rand() % 6;
        int s = rand() % 1001;
        unsigned int movingTime = 200 + rand() % 19800; // Additional time to move into blocked state
      

        processTable[pcb_index].eventWaitSec = systemClock->seconds + r;
        processTable[pcb_index].eventWaitNano = systemClock->nanoseconds + s;
        if (processTable[pcb_index].eventWaitNano >= 1000000000) {
          processTable[pcb_index].eventWaitNano -= 1000000000;
          processTable[pcb_index].eventWaitSec += 1;
        }        
        processTable[pcb_index].blocked = 1;
        advanceClock(inbox.mNum + movingTime);
      } else {
        // Child process used up its time slice
        printf("OSS: Child process %d has used up its time slice\n", processTable[pcb_index].pid);
        processTable[pcb_index].serviceTimeNano += inbox.mNum;
        if (processTable[pcb_index].serviceTimeNano >= 1000000000) {
          processTable[pcb_index].serviceTimeNano -= 1000000000;
          processTable[pcb_index].serviceTimeSeconds += 1;
        }
        advanceClock(inbox.mNum);
      }
    }

  }

  // Print total time taken
  printf("OSS: Total time spent in dispatch was %d seconds and %d nanoseconds\n", systemClock->seconds, systemClock->nanoseconds);

  // Clean up shared memory, message queue, and any other resources used
  shmctl(shmid, IPC_RMID, NULL);
  msgctl(msgget(ftok("oss.c", 1), 0666 | IPC_CREAT), IPC_RMID, NULL);

  return 0;

}