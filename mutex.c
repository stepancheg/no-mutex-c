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

static inline unsigned futex(unsigned *uaddr, unsigned op, unsigned val) {
    return syscall(SYS_futex, uaddr, op, val, 0, 0, 0);
}

static inline bool atomic_compare_exchange(unsigned* ptr, unsigned compare, unsigned exchange) {
    return __atomic_compare_exchange_n(ptr, &compare, exchange,
            0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline unsigned atomic_load(unsigned* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline unsigned atomic_add_fetch(unsigned* ptr, unsigned d) {
    return __atomic_add_fetch(ptr, d, __ATOMIC_SEQ_CST);
}


// mutex library

struct mutex {
    // high bit:  locked
    // rest bits: number of clients
    unsigned locked;
};

#define MUTEX_INIT { 0 };

static const unsigned LOCKED_MASK = 1 << (sizeof(unsigned) * 8 - 1);

void mutex_lock(struct mutex* mutex) {
    // fast track
    if (atomic_compare_exchange(&mutex->locked, 0, 1 | LOCKED_MASK)) {
        return;
    }

    // increment number of waiters, keep locked flag
    for (;;) {
        unsigned v = atomic_load(&mutex->locked);
        unsigned locked = v & LOCKED_MASK;
        unsigned n = v & ~LOCKED_MASK;
        // assert we are not leaking
        if (n > 10000) {
            abort();
        }
        if (atomic_compare_exchange(&mutex->locked, v, (n + 1) | locked)) {
            break;
        }
    }

    // do lock
    for (;;) {
        unsigned v = atomic_load(&mutex->locked);
        if (v & LOCKED_MASK) {
            // locked by someone else, wait
            futex(&mutex->locked, FUTEX_WAIT, v);
        } else {
            // try lock
            if (atomic_compare_exchange(&mutex->locked, v, v | LOCKED_MASK)) {
                break;
            }
        }
    }
}

void mutex_unlock(struct mutex* mutex) {
    // fast track
    if (atomic_compare_exchange(&mutex->locked, 1 | LOCKED_MASK, 0)) {
        return;
    }

    // unlock: clear locked bit and decrement number of lockers
    unsigned r = atomic_add_fetch(&mutex->locked, LOCKED_MASK - 1);
    if (r & LOCKED_MASK) {
        abort();
    }
    futex(&mutex->locked, FUTEX_WAKE, 1);
}


// client code

struct my_thread_params {
    int n;
    struct mutex* lock;
    unsigned* lock_count;
};

void* run_my_thread(void* param) {
    struct my_thread_params* my = (struct my_thread_params*) param;
    for (;;) {
        mutex_lock(my->lock);
        if (atomic_add_fetch(my->lock_count, 1u) > 1u) {
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
    unsigned lock_count = 0;
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
