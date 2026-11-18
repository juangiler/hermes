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

static bool host_false(void) { return false; }
static int host_zero(void) { return 0; }
static void host_noop(void) {}
static void host_open_tab(const char *path, int index, bool focus) {
    (void)path;
    (void)index;
    (void)focus;
}
static const HermesCoreHost probe_host = {
    host_false, host_false, host_false,
    host_noop, host_noop, host_open_tab, host_zero,
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

int main(void) {
    char dir[] = "/tmp/hermes-core-abi-XXXXXX";
    assert(mkdtemp(dir));
    make_files(dir, 8);

    test_host_and_scan(dir);
    test_two_cores(dir);
    test_uri_to_path();
    test_open_uris();

    char p[PATH_MAX];
    for (int i = 0; i < 8; i++) {
        snprintf(p, sizeof p, "%s/f%d.txt", dir, i);
        assert(!unlink(p));
    }
    assert(!rmdir(dir));
    puts("CORE ABI SELFTEST OK");
    return 0;
}
