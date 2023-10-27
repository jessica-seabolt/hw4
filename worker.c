#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

// Message queue struct
struct msgbuf {
  long mType;
  int mNum;
};

struct SystemClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

// Setup shared memory for SystemClock
int shmid;
struct SystemClock *systemClock;

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

int main(int argc, char *argv[]) {
  init_shm();
  
  // Setup message queue
  struct msgbuf outbox, inbox;
  inbox.mType = 1;
  inbox.mNum = 1;
  outbox.mType = 1;
  outbox.mNum = 1;

  key_t msgKey = ftok("oss.c", 1);
  if (msgKey == -1) {
    perror("ftok");
    exit(1);
  }
  int msgQid = msgget(msgKey, 0666 | IPC_CREAT);
  if (msgQid == -1) {
      perror("msgget");
      exit(1);
  }

  srand(getpid()); // Seed for random number generation
  int termProb = 10;
  int interruptProb = 25;

  // Message loop
  while(1) {    

    int termNum = rand() % 100;
    int interruptNum = rand() % 100;

    // Wait for message from oss
    msgrcv(msgQid, &inbox, sizeof(inbox), getpid(), 0);
    printf("Worker %d received message from oss\n", getpid());

    // Check if process should terminate, interrupt, or end
    if (termNum < termProb) {
      outbox.mNum = (1 + rand() % 99) * -0.01 * inbox.mNum;
      msgsnd(msgQid, &outbox, sizeof(outbox), 0);
      // Detach from shared memory
      shmdt(systemClock);
      exit(1);
    } else if (interruptNum < interruptProb) {
      outbox.mNum = (1 + rand() % 99) * 0.01 * inbox.mNum;
      msgsnd(msgQid, &outbox, sizeof(outbox), 0);
    } else {
      outbox.mNum = inbox.mNum;
      msgsnd(msgQid, &outbox, sizeof(outbox), 0);
    }

  }

  return 0;
}