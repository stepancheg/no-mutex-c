#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


// util

static inline bool atomic_compare_exchange(int* ptr, int compare, int exchange) {
    return __atomic_compare_exchange_n(ptr, &compare, exchange,
            0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline void atomic_store(int* ptr, int value) {
    __atomic_store_n(ptr, 0, __ATOMIC_SEQ_CST);
}

static inline int atomic_add_fetch(int* ptr, int d) {
    return __atomic_add_fetch(ptr, d, __ATOMIC_SEQ_CST);
}


// spinlock library

struct spinlock {
    int locked;
};

#define SPINLOCK_INIT { 0 };

void spinlock_lock(struct spinlock* spinlock) {
    while (!atomic_compare_exchange(&spinlock->locked, 0, 1)) {
    }
}

void spinlock_unlock(struct spinlock* spinlock) {
    atomic_store(&spinlock->locked, 0);
}


// client code

struct my_thread_params {
    int n;
    struct spinlock* lock;
    int* lock_count;
};

void* run_my_thread(void* param) {
    struct my_thread_params* my = (struct my_thread_params*) param;
    for (;;) {
        spinlock_lock(my->lock);
        if (atomic_add_fetch(my->lock_count, 1) > 1) {
            fprintf(stderr, "lock is broken\n");
            exit(1);
        }
        printf("thread %d\n", my->n);
        atomic_add_fetch(my->lock_count, -1);
        spinlock_unlock(my->lock);
        getppid();
    }
    return 0;
}

int main() {
    struct spinlock lock = SPINLOCK_INIT;
    int lock_count = 0;
    static const int nthreads = 5;
    pthread_t threads[nthreads];
    struct my_thread_params params[nthreads];
    for (int i = 0; i < nthreads; ++i) {
        params[i].n = i;
        params[i].lock = &lock;
        params[i].lock_count = &lock_count;
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
