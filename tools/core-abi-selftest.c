/* cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation
   tools/core-abi-selftest.c -o /tmp/hermes-core-abi-test -lm -ldl -pthread $(pkg-config --cflags --libs libarchive fuse3 openssl) */
// Built by CMake and linked against the hermes_core static archive. This is
// the test that proves a real C consumer can use the shipped library; the
// engine harness (core-selftest.c) deliberately includes sources instead.
#include "core_api.h"
#include "core_host.h"
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

static bool host_false(void) { return false; }
static int host_zero(void) { return 0; }
static void host_noop(void) {}
static void host_open_tab(const char *path, int index, bool focus) {
    (void)path;
    (void)index;
    (void)focus;
}
static void host_noop_op(int kind, int error_count) { (void)kind; (void)error_count; }
static int g_last_op_kind, g_last_op_errs;
static _Atomic int g_op_completions;
static void host_ops_changed(int kind, int error_count) {
    g_last_op_kind = kind;
    g_last_op_errs = error_count;
    atomic_fetch_add(&g_op_completions, 1);
}
static const HermesCoreHost probe_host = {
    host_false, host_false, host_false,
    host_noop, host_noop, host_open_tab, host_zero,
    host_noop_op,
};

#define MAX_RECORDED_PATHS 8
static char g_opened_paths[MAX_RECORDED_PATHS][256];
static int g_opened_indexes[MAX_RECORDED_PATHS];
static bool g_opened_focus[MAX_RECORDED_PATHS];
static int g_opened_count;
static void host_record_open_tab(const char *path, int index, bool focus) {
    assert(g_opened_count < MAX_RECORDED_PATHS);
    snprintf(g_opened_paths[g_opened_count], sizeof g_opened_paths[0], "%s", path);
    g_opened_indexes[g_opened_count] = index;
    g_opened_focus[g_opened_count] = focus;
    g_opened_count++;
}
static const HermesCoreHost recording_host = {
    host_false, host_false, host_false,
    host_noop, host_noop, host_record_open_tab, host_zero,
    host_ops_changed,
};

static _Atomic int wakes_single, wakes_a, wakes_b;
static int user_a, user_b;
static void wake_single(void *user) { (void)user; atomic_fetch_add(&wakes_single, 1); }
static void wake_a(void *user) { assert(user == &user_a); atomic_fetch_add(&wakes_a, 1); }
static void wake_b(void *user) { assert(user == &user_b); atomic_fetch_add(&wakes_b, 1); }

static void make_files(const char *dir, int n) {
    char p[PATH_MAX];
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s/f%d.txt", dir, i);
        int fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        assert(fd >= 0);
        assert(write(fd, "x", 1) == 1);
        assert(close(fd) == 0);
    }
}

static void wait_for(_Atomic int *counter, int before) {
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < 10000 && atomic_load(counter) == before; i++)
        nanosleep(&pause, NULL);
}

static void test_host_and_scan(const char *dir) {
    // A stack-local table must be safe: the ABI copies it into the library.
    {
        HermesCoreHost temporary = probe_host;
        hermes_core_set_host(&temporary);
    }
    HermesCore *core = hermes_core_create(wake_single, NULL);
    assert(core);

    HermesListing out;
    assert(hermes_core_scan(core, dir, &out) == 0);
    assert(out.count == 8);
    bool found = false;
    for (size_t i = 0; i < out.count; i++) {
        if (!strcmp(out.items[i].name, "f3.txt")) {
            assert(out.items[i].name_len == 6);
            found = true;
        }
    }
    assert(found);
    hermes_listing_free(&out);
    assert(out.items == NULL && out.count == 0);
    wait_for(&wakes_single, 0);
    assert(atomic_load(&wakes_single) >= 1);

    hermes_core_destroy(core);
}

static void test_two_cores(const char *dir) {
    HermesCore *a = hermes_core_create(wake_a, &user_a);
    HermesCore *b = hermes_core_create(wake_b, &user_b);
    assert(a && b && a != b);

    int a_before = atomic_load(&wakes_a);
    int b_before = atomic_load(&wakes_b);

    // B is newest, so its hook is installed. Destroying it must restore A's
    // wake pair; a stale B callback would assert on its user pointer.
    hermes_core_destroy(b);
    hermes_core_drain(b); // destroyed handle: safe no-op

    HermesListing out;
    assert(hermes_core_scan(a, dir, &out) == 0);
    hermes_listing_free(&out);
    wait_for(&wakes_a, a_before);
    assert(atomic_load(&wakes_a) > a_before);
    assert(atomic_load(&wakes_b) == b_before);

    hermes_core_destroy(a);
}

static void test_uri_to_path(void) {
    char out[256];
    assert(!hermes_core_uri_to_path("file:///tmp/a%20b.txt", out, sizeof out));
    assert(!strcmp(out, "/tmp/a b.txt"));
    assert(!hermes_core_uri_to_path("file:///tmp/a%2Fb.txt", out, sizeof out));
    assert(!strcmp(out, "/tmp/a/b.txt"));
    assert(hermes_core_uri_to_path("https://example.com", out, sizeof out) == -1);
    assert(hermes_core_uri_to_path("file://", out, sizeof out) == -1);
    assert(hermes_core_uri_to_path("file:///tmp/a%2", out, sizeof out) == -1);
    assert(hermes_core_uri_to_path("file:///tmp/a", out, 5) == -1);
    assert(hermes_core_uri_to_path(NULL, out, sizeof out) == -1);
    assert(hermes_core_uri_to_path("file:///tmp/a", NULL, sizeof out) == -1);
}

static void test_open_uris(void) {
    hermes_core_set_host(&recording_host);
    HermesCore *core = hermes_core_create(wake_single, NULL);
    assert(core);

    const char *uris[] = {"file:///tmp/a%20b.txt", "file:///tmp/c%2Fd.txt"};
    assert(hermes_core_open_uris(core, uris, 2, true) == 2);
    assert(g_opened_count == 2);
    assert(!strcmp(g_opened_paths[0], "/tmp/a b.txt"));
    assert(!strcmp(g_opened_paths[1], "/tmp/c/d.txt"));
    assert(g_opened_indexes[0] == -1 && g_opened_indexes[1] == -1);
    assert(g_opened_focus[0] && g_opened_focus[1]);

    // A bad URI anywhere in the batch rejects it before any host callback.
    g_opened_count = 0;
    const char *mixed[] = {"file:///tmp/ok.txt", "https://example.com"};
    assert(hermes_core_open_uris(core, mixed, 2, true) == -1);
    assert(g_opened_count == 0);

    hermes_core_destroy(core);
    assert(hermes_core_open_uris(core, uris, 2, true) == -1);
    assert(g_opened_count == 0);
    hermes_core_set_host(&probe_host);
}

static void write_content(const char *path, const char *data, size_t n) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(write(fd, data, n) == (ssize_t)n);
    assert(close(fd) == 0);
}

static void wait_for_op_completion(HermesCore *core, int before) {
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < 10000 && atomic_load(&g_op_completions) == before; i++) {
        hermes_core_drain(core);
        nanosleep(&pause, NULL);
    }
}

static void test_scan_sorted(void) {
    char dir[] = "/tmp/hermes-core-abi-sort-XXXXXX";
    assert(mkdtemp(dir));
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/a.txt", dir);
    write_content(p, "a", 1);
    snprintf(p, sizeof p, "%s/b.txt", dir);
    write_content(p, "b", 1);
    snprintf(p, sizeof p, "%s/zdir", dir);
    assert(mkdir(p, 0755) == 0);

    HermesCoreHost temporary = probe_host;
    hermes_core_set_host(&temporary);
    HermesCore *core = hermes_core_create(wake_single, NULL);
    assert(core);

    HermesListing out;
    assert(hermes_core_scan_sorted(core, dir, HERMES_SORT_NAME, false, &out) == 0);
    assert(out.count == 3);
    assert(out.items[0].flags & HERMES_ENTRY_DIR);
    assert(!strcmp(out.items[0].name, "zdir"));
    assert(!strcmp(out.items[1].name, "a.txt"));
    assert(!strcmp(out.items[2].name, "b.txt"));
    hermes_listing_free(&out);

    char sizedir[] = "/tmp/hermes-core-abi-size-XXXXXX";
    assert(mkdtemp(sizedir));
    snprintf(p, sizeof p, "%s/a.txt", sizedir);
    write_content(p, "a", 1);
    snprintf(p, sizeof p, "%s/b.txt", sizedir);
    write_content(p, "bb", 2);
    HermesListing out2;
    assert(hermes_core_scan_sorted(core, sizedir, HERMES_SORT_SIZE, true, &out2) == 0);
    assert(out2.count == 2);
    assert(!strcmp(out2.items[0].name, "b.txt"));
    assert(!strcmp(out2.items[1].name, "a.txt"));
    snprintf(p, sizeof p, "%s/a.txt", sizedir);
    write_content(p, "aaaaa", 5);
    HermesListing out3;
    assert(hermes_core_scan_sorted(core, sizedir, HERMES_SORT_SIZE, true, &out3) == 0);
    assert(!strcmp(out3.items[0].name, "a.txt"));
    assert(!strcmp(out3.items[1].name, "b.txt"));
    hermes_listing_free(&out2);
    hermes_listing_free(&out3);

    snprintf(p, sizeof p, "%s/a.txt", dir);
    assert(!unlink(p));
    snprintf(p, sizeof p, "%s/b.txt", dir);
    assert(!unlink(p));
    snprintf(p, sizeof p, "%s/zdir", dir);
    assert(!rmdir(p));
    assert(!rmdir(dir));
    snprintf(p, sizeof p, "%s/a.txt", sizedir);
    assert(!unlink(p));
    snprintf(p, sizeof p, "%s/b.txt", sizedir);
    assert(!unlink(p));
    assert(!rmdir(sizedir));

    hermes_core_destroy(core);
}

static void test_ops_mkdir_rename_remove(void) {
    char dir[] = "/tmp/hermes-core-abi-ops-XXXXXX";
    assert(mkdtemp(dir));
    char xdg[] = "/tmp/hermes-core-abi-xdg-XXXXXX";
    assert(mkdtemp(xdg));
    setenv("XDG_DATA_HOME", xdg, 1);
    setenv("XDG_CONFIG_HOME", xdg, 1);

    HermesCoreHost temporary = recording_host;
    hermes_core_set_host(&temporary);
    HermesCore *core = hermes_core_create(wake_single, NULL);
    assert(core);

    int before = atomic_load(&g_op_completions);
    assert(hermes_core_mkdir(core, dir, "newdir") == 0);
    wait_for_op_completion(core, before);
    assert(atomic_load(&g_op_completions) == before + 1);
    assert(g_last_op_kind == HERMES_OP_MKDIR);
    assert(g_last_op_errs == 0);
    char np[PATH_MAX];
    snprintf(np, sizeof np, "%s/newdir", dir);
    assert(access(np, F_OK) == 0);

    before = atomic_load(&g_op_completions);
    char np2[PATH_MAX];
    snprintf(np2, sizeof np2, "%s/renamed", dir);
    assert(hermes_core_rename(core, np, np2) == 0);
    wait_for_op_completion(core, before);
    assert(atomic_load(&g_op_completions) == before + 1);
    assert(g_last_op_kind == HERMES_OP_RENAME);
    assert(g_last_op_errs == 0);
    assert(access(np2, F_OK) == 0);

    before = atomic_load(&g_op_completions);
    const char *paths[] = {np2};
    assert(hermes_core_remove(core, paths, 1, false) == 0);
    wait_for_op_completion(core, before);
    assert(atomic_load(&g_op_completions) == before + 1);
    assert(g_last_op_kind == HERMES_OP_DELETE);
    assert(g_last_op_errs == 0);
    assert(access(np2, F_OK) != 0);

    hermes_core_destroy(core);
}

static void test_ops_copy_move_conflict_policy(void) {
    char dir[] = "/tmp/hermes-core-abi-copy-XXXXXX";
    assert(mkdtemp(dir));
    char xdg[] = "/tmp/hermes-core-abi-xdg2-XXXXXX";
    assert(mkdtemp(xdg));
    setenv("XDG_DATA_HOME", xdg, 1);
    setenv("XDG_CONFIG_HOME", xdg, 1);

    char src[PATH_MAX];
    snprintf(src, sizeof src, "%s/a.txt", dir);
    write_content(src, "hello", 5);
    char dst[PATH_MAX];
    snprintf(dst, sizeof dst, "%s/dst", dir);
    assert(mkdir(dst, 0755) == 0);
    char dstfile[PATH_MAX];
    snprintf(dstfile, sizeof dstfile, "%s/dst/a.txt", dir);
    write_content(dstfile, "original", 8);

    HermesCoreHost temporary = recording_host;
    hermes_core_set_host(&temporary);
    HermesCore *core = hermes_core_create(wake_single, NULL);
    assert(core);

    int before = atomic_load(&g_op_completions);
    const char *paths[] = {src};
    assert(hermes_core_copy(core, paths, 1, dst, false, HERMES_CONFLICT_RENAME) == 0);
    wait_for_op_completion(core, before);
    assert(atomic_load(&g_op_completions) == before + 1);
    assert(g_last_op_kind == HERMES_OP_COPY);
    assert(g_last_op_errs == 0);
    // the conflict policy renamed the original aside instead of overwriting it
    char saved[PATH_MAX];
    snprintf(saved, sizeof saved, "%s/dst/a (2).txt", dir);
    assert(access(saved, F_OK) == 0);
    assert(access(dstfile, F_OK) == 0);

    before = atomic_load(&g_op_completions);
    char dst2[PATH_MAX];
    snprintf(dst2, sizeof dst2, "%s/dst2", dir);
    assert(mkdir(dst2, 0755) == 0);
    assert(hermes_core_copy(core, paths, 1, dst2, true, HERMES_CONFLICT_RENAME) == 0);
    wait_for_op_completion(core, before);
    assert(atomic_load(&g_op_completions) == before + 1);
    assert(g_last_op_kind == HERMES_OP_MOVE);
    assert(g_last_op_errs == 0);
    // the move took the copy: source gone, destination populated
    assert(access(src, F_OK) != 0);
    char moved[PATH_MAX];
    snprintf(moved, sizeof moved, "%s/dst2/a.txt", dir);
    assert(access(moved, F_OK) == 0);

    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/dst/a (2).txt", dir);
    assert(!unlink(p));
    snprintf(p, sizeof p, "%s/dst/a.txt", dir);
    assert(!unlink(p));
    snprintf(p, sizeof p, "%s/dst2/a.txt", dir);
    assert(!unlink(p));
    assert(!rmdir(dst2));
    assert(!rmdir(dst));
    assert(!rmdir(dir));

    hermes_core_destroy(core);
}

static void test_operations_changed_hook(void) {
    char dir[] = "/tmp/hermes-core-abi-hook-XXXXXX";
    assert(mkdtemp(dir));
    HermesCoreHost temporary = recording_host;
    hermes_core_set_host(&temporary);
    HermesCore *core = hermes_core_create(wake_single, NULL);
    assert(core);

    int before = atomic_load(&g_op_completions);
    const char *bad[] = {NULL};
    assert(hermes_core_remove(core, bad, 1, false) == -1);
    assert(atomic_load(&g_op_completions) == before); // invalid input: no completion

    before = atomic_load(&g_op_completions);
    assert(hermes_core_mkdir(core, dir, "hooked") == 0);
    wait_for_op_completion(core, before);
    assert(atomic_load(&g_op_completions) == before + 1);
    assert(g_last_op_kind == HERMES_OP_MKDIR);
    assert(g_last_op_errs == 0);

    char np[PATH_MAX];
    snprintf(np, sizeof np, "%s/hooked", dir);
    assert(!rmdir(np));
    assert(!rmdir(dir));
    hermes_core_destroy(core);
}

int main(void) {
    char dir[] = "/tmp/hermes-core-abi-XXXXXX";
    assert(mkdtemp(dir));
    make_files(dir, 8);

    test_host_and_scan(dir);
    test_two_cores(dir);
    test_uri_to_path();
    test_open_uris();
    test_scan_sorted();
    test_ops_mkdir_rename_remove();
    test_ops_copy_move_conflict_policy();
    test_operations_changed_hook();

    char p[PATH_MAX];
    for (int i = 0; i < 8; i++) {
        snprintf(p, sizeof p, "%s/f%d.txt", dir, i);
        assert(!unlink(p));
    }
    assert(!rmdir(dir));
    puts("CORE ABI SELFTEST OK");
    return 0;
}
