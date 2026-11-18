// core_api.c - the small C ABI exposed to non-SDL frontends.
// Included by src/hermes_core.c (core library) and by tools/core-selftest.c.

struct HermesCore {
    HermesWakeFn wake;
    void *user;
    bool destroyed;
};

// drain() takes an opaque handle and never dereferences it, so calling it with
// a handle from an already-destroyed core is a safe no-op.
static _Atomic bool g_core_destroyed;

HermesCore *hermes_core_create(HermesWakeFn wake, void *user) {
    HermesCore *core = xcalloc(1, sizeof *core);
    core->wake = wake;
    core->user = user;
    atomic_store(&g_core_destroyed, false);
    pool_set_wake(wake, user);
    pool_init();
    return core;
}

void hermes_core_destroy(HermesCore *core) {
    if (!core || core->destroyed) return;
    core->destroyed = true;
    atomic_store(&g_core_destroyed, true);
    pool_set_wake(NULL, NULL);
    main_drain();
    free(core);
}

void hermes_core_drain(HermesCore *core) {
    (void)core;
    if (atomic_load(&g_core_destroyed)) return;
    main_drain();
}

int hermes_core_scan(HermesCore *core, const char *path, HermesListing *out) {
    if (!core || core->destroyed || !path || !out) return -1;
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
