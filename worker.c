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
} message;

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
    
    // Get message queue
    key_t msgKey = ftok("oss.c", 1);
    int msgQid = msgget(msgKey, 0666 | IPC_CREAT);
    if (msgQid == -1) {
        perror("msgget");
        exit(1);
    }
    
    // Send message to oss
    message.mType = 1;
    message.mNum = 1;
    
    // Wait for message from oss
    msgrcv(msgQid, &message, sizeof(message), getpid(), 0);

    printf("Message received: %d", message.mNum);
    printf("Seconds: %d", systemClock->seconds);
    printf("Nanoseconds: %d", systemClock->nanoseconds);
    
    // Detach from shared memory
    shmdt(systemClock);
    
    return 0;
}