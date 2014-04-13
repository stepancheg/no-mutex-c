#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <linux/futex.h>


// util

static inline int futex(int *uaddr, int op, int val) {
    return syscall(SYS_futex, uaddr, op, val, 0, 0, 0);
}

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


// mutex library

struct mutex {
    // 0 unlocked
    // 1 locked
    int locked;
    // number of threads requesting a lock
    int count;
};

#define MUTEX_INIT { 0, 0 };

void mutex_lock(struct mutex* mutex) {
    atomic_add_fetch(&mutex->count, 1);
    while (!atomic_compare_exchange(&mutex->locked, 0, 1)) {
        futex(&mutex->locked, FUTEX_WAIT, 1);
    }
}

void mutex_unlock(struct mutex* mutex) {
    int left = atomic_add_fetch(&mutex->count, -1);
    atomic_store(&mutex->locked, 0);
    if (left > 0) {
        futex(&mutex->locked, FUTEX_WAKE, 1);
    }
}


// client code

struct my_thread_params {
    int n;
    struct mutex* lock;
    int* lock_count;
};

void* run_my_thread(void* param) {
    struct my_thread_params* my = (struct my_thread_params*) param;
    for (;;) {
        mutex_lock(my->lock);
        if (atomic_add_fetch(my->lock_count, 1) > 1) {
            fprintf(stderr, "lock is broken\n");
            exit(1);
        }
        printf("thread %d\n", my->n);
        atomic_add_fetch(my->lock_count, -1);
        mutex_unlock(my->lock);
    }
    return 0;
}

int main() {
    struct mutex lock = MUTEX_INIT;
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
