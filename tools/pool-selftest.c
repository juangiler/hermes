/* cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
   tools/pool-selftest.c -o /tmp/hermes-pool-test -lm -pthread */
#include "../src/base.h"
#include <assert.h>
#include "../src/pool.c"
static _Atomic int wakes;
static void wake(void *user) { (void)user; atomic_fetch_add(&wakes, 1); }
static _Atomic int ran;
static void main_job(void *arg) { (void)arg; atomic_fetch_add(&ran, 1); }
static void worker_job(void *arg) { (void)arg; main_post(main_job, NULL); }
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
    pool_set_wake(NULL, NULL);
    return 0;
}
