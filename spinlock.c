#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>


// spinlock library

struct spinlock {
    int locked;
};

#define SPINLOCK_INIT { 0 };

void spinlock_lock(struct spinlock* spinlock) {
    while (__atomic_exchange_n(&spinlock->locked, 1, __ATOMIC_SEQ_CST) == 1) {
    }
}

void spinlock_unlock(struct spinlock* spinlock) {
    __atomic_store_n(&spinlock->locked, 0, __ATOMIC_SEQ_CST);
}


// client code

struct my_thread_params {
    int n;
    struct spinlock* lock;
};

void* run_my_thread(void* param) {
    struct my_thread_params* my = (struct my_thread_params*) param;
    for (;;) {
        spinlock_lock(my->lock);
        printf("thread %d\n", my->n);
        printf("thread %d again\n", my->n);
        spinlock_unlock(my->lock);
    }
    return 0;
}

int main() {
    struct spinlock lock = SPINLOCK_INIT;
    static const int nthreads = 3;
    pthread_t threads[nthreads];
    struct my_thread_params params[nthreads];
    for (int i = 0; i < nthreads; ++i) {
        params[i].n = i;
        params[i].lock = &lock;
        int r = pthread_create(&threads[i], 0, &run_my_thread, &params[i]);
        if (r != 0) {
            fprintf(stderr, "failed to start thread: %s\n", strerror(r));
            exit(1);
        }
    }
    for (int i = 0; i < nthreads; ++i) {
        int r = pthread_join(threads[i], 0);
        if (r != 0) {
            fprintf(stderr, "failed to join thread: %s\n", strerror(r));
            exit(1);
        }
    }
    return 0;
}
