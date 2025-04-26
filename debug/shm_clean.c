#include <sys/shm.h>    // shmat, shmdt, shmctl

#define countof(array) ((int)sizeof(array) / (int)sizeof((array)[0]))

int main(void) {
    int ids[] = {
    };

    for (int i = 0; i < countof(ids); i += 1) {
        void *address = shmat(ids[i], 0, 0);
        shmdt(address);
        shmctl(ids[i], IPC_RMID, 0);
    }
}
