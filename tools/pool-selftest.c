/* cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
   tools/pool-selftest.c -o /tmp/hermes-pool-test -lm -pthread */
#include "../src/base.h"
#include <assert.h>
#include <sched.h>
#include "../src/pool.c"
static _Atomic int wakes;
static void wake(void *user) { (void)user; atomic_fetch_add(&wakes, 1); }
static _Atomic int ran;
static void main_job(void *arg) { (void)arg; atomic_fetch_add(&ran, 1); }
static void worker_job(void *arg) { (void)arg; main_post(main_job, NULL); }

// Replacing the wake hook while workers are posting must publish both fields
// together; ThreadSanitizer flags the old unsynchronised pair.
static _Atomic int wakes_a, wakes_b, stress_posts;
static _Atomic bool stop_flip;
static void wake_a(void *user) { (void)user; atomic_fetch_add(&wakes_a, 1); }
static void wake_b(void *user) { (void)user; atomic_fetch_add(&wakes_b, 1); }
static void stress_main(void *arg) { (void)arg; atomic_fetch_add(&stress_posts, 1); }
static void stress_worker(void *arg) { (void)arg; main_post(stress_main, NULL); }
static void *flipper(void *arg) {
    (void)arg;
    while (!atomic_load(&stop_flip)) {
        pool_set_wake(wake_a, NULL);
        sched_yield();
        pool_set_wake(wake_b, NULL);
        sched_yield();
    }
    return NULL;
}

int main(void) {
    pool_set_wake(wake, NULL);
    pool_init();
    pool_submit(worker_job, NULL);
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < 10000 && (!atomic_load(&ran) || !atomic_load(&wakes)); i++) {
        main_drain();
        nanosleep(&pause, NULL);
    }
    assert(atomic_load(&ran) == 1);
    assert(atomic_load(&wakes) >= 1);

    // Install the first hook before starting the flipper, otherwise early jobs
    // in the stress batch would still invoke the previous test's wake hook.
    pool_set_wake(wake_a, NULL);
    pthread_t t;
    assert(!pthread_create(&t, NULL, flipper, NULL));
    for (int i = 0; i < 500; i++) pool_submit(stress_worker, NULL);
    for (int i = 0; i < 20000 && atomic_load(&stress_posts) < 500; i++) {
        main_drain();
        nanosleep(&pause, NULL);
    }
    atomic_store(&stop_flip, true);
    assert(!pthread_join(t, NULL));
    assert(atomic_load(&stress_posts) == 500);
    // main_post() queues the job before calling main_wake(), so observing all
    // jobs drained can still leave a few workers before their wake callback.
    for (int i = 0; i < 20000 &&
         atomic_load(&wakes_a) + atomic_load(&wakes_b) < 500; i++)
        nanosleep(&pause, NULL);
    assert(atomic_load(&wakes_a) + atomic_load(&wakes_b) >= 500);

    pool_set_wake(NULL, NULL);
    return 0;
}
