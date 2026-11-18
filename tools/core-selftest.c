/* cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation
   tools/core-selftest.c -o /tmp/hermes-core-test -lm -ldl -pthread $(pkg-config --cflags --libs libarchive fuse3 openssl) */
#include "../src/base.h"
#include "../src/core_api.h"
#include "../src/core_host.h"
#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <locale.h>
#include <regex.h>

static struct { bool selftest; bool screenshot; } A = {.selftest = true};

// App callbacks are declared (but not defined) by the engine files below; the
// no-op definitions live after the includes so they can see the engine types.

#include "../src/pool.c"
#include "../src/ipc.c"
#include "../src/fs.c"
#include "../src/local_shares.c"
#include "../src/ops.c"
#include "../src/archive.c"
#include "../src/search.c"
#include "../src/diskuse.c"
#include "../src/compare.c"
#include "../src/snapshots.c"
#include "../src/svg_preview.c"
#include "../src/preview.c"
#include "../src/usb.c"
#include "../src/plist.c"
#include "../src/iphone.c"
#include "../src/mtp.c"
#include "../src/devfs.c"
#include "../src/devices.c"
#include "../src/git.c"
#include "../src/apps.c"
#include "../src/cloud_identity.c"
#include "../src/core_api.c"

static _Atomic int op_finished_count;
static void op_finished(void *arg) {
    op_free((Op *)arg);
    atomic_fetch_add(&op_finished_count, 1);
}
static void search_batch_cb(void *arg) { (void)arg; }
static void content_batch_cb(void *arg) { (void)arg; }
static void dirsize_done_cb(void *arg) { (void)arg; }
static void du_scan_exit_cb(void *arg) { (void)arg; }
static void cmp_job_done_cb(void *arg) { (void)arg; }
static void preview_done_cb(void *arg) { (void)arg; }
static void thumb_done_cb(void *arg) { (void)arg; }
static void dev_job_done(void *arg) { (void)arg; }
static void dev_phone_wait(void *arg) { free(arg); }
static void dev_net_question(void *arg) { free(arg); }
static bool app_selftest(void) { return A.selftest; }
static bool ssh_target_uri(const char *uri, SshTarget *t) {
    (void)uri;
    if (t) memset(t, 0, sizeof *t);
    return false;
}
static void git_resolve_done(void *arg) { (void)arg; }
static void git_job_done(void *arg) { (void)arg; }
static void git_subs_done(void *arg) { (void)arg; }
static void spawn_terminal(const char *dir, char *const cmd[]) {
    (void)dir;
    (void)cmd;
}

static void write_file(const char *path, const char *data, size_t n) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(write(fd, data, n) == (ssize_t)n);
    assert(close(fd) == 0);
}

static void drain_until(_Atomic int *state, int done, int limit) {
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < limit && atomic_load(state) < done; i++) {
        main_drain();
        nanosleep(&pause, NULL);
    }
}

static void check_entry(Listing *L, const char *name) {
    for (u32 i = 0; i < L->count; i++) {
        if (!strcmp(L->entries[i].name, name)) {
            assert(L->entries[i].name_len == strlen(name));
            return;
        }
    }
    fprintf(stderr, "missing entry: %s\n", name);
    abort();
}

static void test_scan_names(const char *dir) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/a.txt", dir);
    write_file(p, "a", 1);
    snprintf(p, sizeof p, "%s/b b.txt", dir);
    write_file(p, "bb", 2);
    snprintf(p, sizeof p, "%s/caf\xc3\xa9.txt", dir);
    write_file(p, "c", 1);
    char longname[256];
    memset(longname, 'z', 255);
    longname[255] = 0;
    snprintf(p, sizeof p, "%s/%s", dir, longname);
    write_file(p, "l", 1);

    Listing *L = listing_new(dir, SORT_NAME, false, true);
    listing_start(L);
    drain_until(&L->state, LST_COMPLETE, 10000);
    assert(atomic_load(&L->state) == LST_COMPLETE);
    assert(L->count == 4);
    check_entry(L, "a.txt");
    check_entry(L, "b b.txt");
    check_entry(L, "caf\xc3\xa9.txt");
    check_entry(L, longname);
    listing_release(L);
}

static void test_scan_empty_and_symlink(const char *plain_dir, const char *link_dir) {
    Listing *L = listing_new(plain_dir, SORT_NAME, false, true);
    listing_start(L);
    drain_until(&L->state, LST_COMPLETE, 10000);
    assert(atomic_load(&L->state) == LST_COMPLETE);
    assert(L->count == 0);
    listing_release(L);

    // A self-referencing symlink must be listed once, not followed forever.
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/loop", link_dir);
    assert(symlink("loop", p) == 0);
    L = listing_new(link_dir, SORT_NAME, false, true);
    listing_start(L);
    drain_until(&L->state, LST_COMPLETE, 10000);
    assert(atomic_load(&L->state) == LST_COMPLETE);
    assert(L->count == 1);
    check_entry(L, "loop");
    assert(L->entries[0].flags & ENT_LINK);
    listing_release(L);
}

static void test_scan_denied(const char *dir) {
    if (geteuid() == 0) {
        fprintf(stderr, "SKIP permission-denied case (running as root)\n");
        return;
    }
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/secret", dir);
    assert(mkdir(p, 0700) == 0);
    char f[PATH_MAX];
    snprintf(f, sizeof f, "%s/x", p);
    write_file(f, "x", 1);
    assert(chmod(p, 0000) == 0);

    Listing *L = listing_new(p, SORT_NAME, false, true);
    listing_start(L);
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < 10000 && atomic_load(&L->state) < LST_ERROR; i++) {
        main_drain();
        nanosleep(&pause, NULL);
    }
    assert(atomic_load(&L->state) == LST_ERROR);
    assert(L->err != 0);
    listing_release(L);
    assert(chmod(p, 0700) == 0);
}

static void test_copy_cross_device(const char *src_dir, const char *dst_dir) {
    char src[PATH_MAX], dst[PATH_MAX];
    snprintf(src, sizeof src, "%s/copy-me.bin", src_dir);
    snprintf(dst, sizeof dst, "%s/copy-me.bin", dst_dir);
    const char payload[] = "cross-device copy payload";
    write_file(src, payload, sizeof payload - 1);

    int finished_before = atomic_load(&op_finished_count);
    char *srcs[] = {src};
    Op *op = op_new(OP_COPY, srcs, 1, dst_dir);
    assert(op);
    op_launch(op);
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < 100000 && atomic_load(&op->phase) != OPS_DONE; i++) {
        main_drain();
        nanosleep(&pause, NULL);
    }
    assert(atomic_load(&op->phase) == OPS_DONE);

    // ops.c publishes OPS_DONE before main_post(op_finished, op). Keep draining
    // until the stub runs, so its op_free() ownership is actually exercised.
    for (int i = 0; i < 10000 && atomic_load(&op_finished_count) == finished_before; i++) {
        main_drain();
        nanosleep(&pause, NULL);
    }
    assert(atomic_load(&op_finished_count) == finished_before + 1);

    int a = open(src, O_RDONLY), b = open(dst, O_RDONLY);
    assert(a >= 0 && b >= 0);
    char ba[sizeof payload], bb[sizeof payload];
    ssize_t na = read(a, ba, sizeof ba), nb = read(b, bb, sizeof bb);
    assert(na == (ssize_t)sizeof payload - 1 && nb == na);
    assert(!memcmp(ba, bb, (size_t)na));
    close(a);
    close(b);
}

static void test_fuzzy_and_natcmp(void) {
    u16 pos[32] = {0};
    assert(fuzzy_match("README.md", 9, "rmd", 3, pos) > 0);
    assert(natcmp("file2", "file10") < 0);
}

static void test_xxh64(void) {
    Xxh64 h;
    xxh64_init(&h);
    xxh64_update(&h, (const u8 *)"abc", 3);
    assert(xxh64_final(&h) == 0x44BC2CF5AD770999ull);
}

static void test_core_host_defaults(void) {
    assert(!g_core_host->is_selftest());
    assert(!g_core_host->is_screenshot());
    assert(!g_core_host->window_hidden());
    assert(g_core_host->current_tab() == 0);
    g_core_host->mark_settings_dirty();
    g_core_host->request_redraw();
    g_core_host->open_tab("/tmp", 0, true);
}

static _Atomic int abi_wakes;
static void abi_wake(void *user) { (void)user; atomic_fetch_add(&abi_wakes, 1); }
static _Atomic int abi_jobs;
static void abi_main_job(void *arg) { (void)arg; atomic_fetch_add(&abi_jobs, 1); }
static void abi_worker_job(void *arg) { (void)arg; main_post(abi_main_job, NULL); }

static void test_public_abi(const char *dir) {
    HermesCore *core = hermes_core_create(abi_wake, NULL);
    assert(core);

    HermesListing out;
    assert(hermes_core_scan(core, dir, &out) == 0);
    assert(out.count == 4);
    bool found = false;
    for (size_t i = 0; i < out.count; i++) {
        if (!strcmp(out.items[i].name, "a.txt")) {
            assert(out.items[i].name_len == 5);
            found = true;
        }
    }
    assert(found);
    hermes_listing_free(&out);
    assert(out.items == NULL && out.count == 0);

    // Exercise the public drain path, then destroy with another job queued:
    // a late callback must not touch freed core state, and drain after destroy
    // must stay a safe no-op.
    struct timespec pause = {0, 1000000};
    pool_submit(abi_worker_job, NULL);
    for (int i = 0; i < 10000 && !atomic_load(&abi_jobs); i++) {
        hermes_core_drain(core);
        nanosleep(&pause, NULL);
    }
    assert(atomic_load(&abi_jobs) == 1);
    assert(atomic_load(&abi_wakes) >= 1);
    pool_submit(abi_worker_job, NULL);
    hermes_core_destroy(core);
    hermes_core_drain(core);
}

static _Atomic int multicore_jobs;
static void multicore_job(void *arg) { (void)arg; atomic_fetch_add(&multicore_jobs, 1); }
static void multicore_worker(void *arg) { (void)arg; main_post(multicore_job, NULL); }

static void test_two_cores(void) {
    HermesCore *a = hermes_core_create(abi_wake, NULL);
    HermesCore *b = hermes_core_create(abi_wake, NULL);
    assert(a && b && a != b);

    hermes_core_destroy(a);
    // Destroyed handles are rejected by pointer lookup, never dereferenced.
    hermes_core_drain(a);

    // Destroying A must not disable drain through B.
    int before = atomic_load(&multicore_jobs);
    struct timespec pause = {0, 1000000};
    pool_submit(multicore_worker, NULL);
    for (int i = 0; i < 10000 && atomic_load(&multicore_jobs) == before; i++) {
        hermes_core_drain(b);
        nanosleep(&pause, NULL);
    }
    assert(atomic_load(&multicore_jobs) == before + 1);

    // Creating another core after a destroy must not resurrect the dead handle.
    hermes_core_drain(a);
    assert(atomic_load(&multicore_jobs) == before + 1);

    hermes_core_destroy(b);
}

static void cleanup_dir(const char *dir, const char *longname) {
    char p[PATH_MAX];
    const char *names[] = {"a.txt", "b b.txt", "caf\xc3\xa9.txt", "copy-me.bin", "secret/x", "loop"};
    for (size_t i = 0; i < ARRAY_COUNT(names); i++) {
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        unlink(p);
    }
    if (longname) {
        snprintf(p, sizeof p, "%s/%s", dir, longname);
        unlink(p);
    }
    snprintf(p, sizeof p, "%s/secret", dir);
    rmdir(p);
    rmdir(dir);
}

int main(void) {
    pool_set_wake(NULL, NULL);
    pool_init();

    test_fuzzy_and_natcmp();
    test_xxh64();
    test_core_host_defaults();

    char dir[] = "/tmp/hermes-core-XXXXXX";
    assert(mkdtemp(dir));
    char plain[] = "/tmp/hermes-core-plain-XXXXXX";
    assert(mkdtemp(plain));
    char empty[] = "/tmp/hermes-core-empty-XXXXXX";
    assert(mkdtemp(empty));
    char shm[] = "/dev/shm/hermes-core-XXXXXX";
    assert(mkdtemp(shm));

    char longname[256];
    memset(longname, 'z', 255);
    longname[255] = 0;
    test_scan_names(dir);
    test_public_abi(dir);
    test_two_cores();
    test_scan_empty_and_symlink(plain, empty);
    test_scan_denied(dir);
    test_copy_cross_device(dir, shm);

    cleanup_dir(dir, longname);
    cleanup_dir(plain, NULL);
    cleanup_dir(empty, NULL);
    cleanup_dir(shm, NULL);
    printf("CORE SELFTEST OK\n");
    return 0;
}
