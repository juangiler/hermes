// core_api.c - the small C ABI exposed to non-SDL frontends.
// Included by src/hermes_core.c (core library) and by tools/core-selftest.c.
#include "core_api.h"
#include "core_host.h"

struct HermesCore {
    HermesWakeFn wake;
    void *user;
    bool destroyed;
    struct HermesCore *next;
};

// Handles are kept in a registry so drain() can reject a destroyed handle by
// pointer comparison without ever dereferencing freed memory.
static pthread_mutex_t g_registry_mtx = PTHREAD_MUTEX_INITIALIZER;
static HermesCore *g_cores;
static int g_live_cores;

// The host table is copied, so a frontend may pass a temporary. Callers must
// still install it before starting jobs: engine workers read it without a lock.
static pthread_mutex_t g_host_mtx = PTHREAD_MUTEX_INITIALIZER;
static HermesCoreHost g_owned_host;

static bool core_live(HermesCore *core) {
    if (!core) return false;
    pthread_mutex_lock(&g_registry_mtx);
    bool live = false;
    for (HermesCore *c = g_cores; c; c = c->next) {
        if (c == core && !c->destroyed) { live = true; break; }
    }
    pthread_mutex_unlock(&g_registry_mtx);
    return live;
}

void hermes_core_set_host(const struct HermesCoreHost *host) {
    pthread_mutex_lock(&g_host_mtx);
    if (host) {
        g_owned_host = *host;
        g_core_host = &g_owned_host;
    } else {
        g_core_host = &hermes_core_host_defaults;
    }
    pthread_mutex_unlock(&g_host_mtx);
}

HermesCore *hermes_core_create(HermesWakeFn wake, void *user) {
    HermesCore *core = xcalloc(1, sizeof *core);
    core->wake = wake;
    core->user = user;
    pthread_mutex_lock(&g_registry_mtx);
    core->next = g_cores;
    g_cores = core;
    g_live_cores++;
    pthread_mutex_unlock(&g_registry_mtx);
    pool_set_wake(wake, user);
    pool_init();
    return core;
}

void hermes_core_destroy(HermesCore *core) {
    if (!core) return;
    pthread_mutex_lock(&g_registry_mtx);
    HermesCore **link = &g_cores;
    while (*link && *link != core) link = &(*link)->next;
    if (!*link) { pthread_mutex_unlock(&g_registry_mtx); return; }
    *link = core->next;
    core->destroyed = true;
    g_live_cores--;
    // The newest live core is the registry head. Restore its hook when the
    // destroyed core was the one that installed it; clear it only when none
    // remain. Otherwise a late callback could use the destroyed core's user.
    HermesWakeFn wake = g_cores ? g_cores->wake : NULL;
    void *user = g_cores ? g_cores->user : NULL;
    pthread_mutex_unlock(&g_registry_mtx);
    pool_set_wake(wake, user);
    main_drain();
    free(core);
}

void hermes_core_drain(HermesCore *core) {
    if (!core_live(core)) return;
    main_drain();
}

int hermes_core_scan(HermesCore *core, const char *path, HermesListing *out) {
    if (!core_live(core) || !path || !out) return -1;
    memset(out, 0, sizeof *out);

    Listing *L = listing_new(path, SORT_NAME, false, true);
    listing_start(L);
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < 10000; i++) {
        int st = atomic_load(&L->state);
        if (st >= LST_COMPLETE || st == LST_ERROR) break;
        main_drain();
        nanosleep(&pause, NULL);
    }
    int st = atomic_load(&L->state);
    if (st != LST_COMPLETE) {
        listing_release(L);
        return -1;
    }

    out->items = xcalloc(L->count ? L->count : 1, sizeof *out->items);
    out->count = L->count;
    for (u32 i = 0; i < L->count; i++) {
        out->items[i].name = strdup(L->entries[i].name);
        out->items[i].name_len = L->entries[i].name_len;
        out->items[i].flags = L->entries[i].flags;
        out->items[i].size = L->entries[i].size;
        out->items[i].mtime = L->entries[i].mtime;
    }
    listing_release(L);
    return 0;
}

void hermes_listing_free(HermesListing *listing) {
    if (!listing) return;
    for (size_t i = 0; i < listing->count; i++) free((void *)listing->items[i].name);
    free(listing->items);
    memset(listing, 0, sizeof *listing);
}
