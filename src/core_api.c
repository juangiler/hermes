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

struct HermesScan {
    Listing *listing;
    HermesScanOptions options;
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

static int engine_sort_key(HermesSortKey key) {
    switch (key) {
    case HERMES_SORT_NAME: return SORT_NAME;
    case HERMES_SORT_DATE: return SORT_DATE;
    case HERMES_SORT_TYPE: return SORT_TYPE;
    case HERMES_SORT_SIZE: return SORT_SIZE;
    case HERMES_SORT_CREATED: return SORT_CREATED;
    case HERMES_SORT_EXT: return SORT_EXT;
    case HERMES_SORT_PERMS: return SORT_PERMS;
    case HERMES_SORT_OWNER: return SORT_OWNER;
    }
    return -1;
}

HermesScan *hermes_core_scan_start(HermesCore *core, const char *path,
                                   const HermesScanOptions *options) {
    if (!core_live(core) || !path || !path[0] || !options) return NULL;
    int key = engine_sort_key(options->key);
    if (key < 0) return NULL;

    HermesScan *scan = xcalloc(1, sizeof *scan);
    scan->options = *options;
    scan->listing = listing_new(path, key, options->desc, options->show_hidden);
    listing_start(scan->listing);
    return scan;
}

int hermes_core_scan_poll(HermesCore *core, HermesScan *scan, HermesListing *out) {
    if (!core_live(core) || !scan || !scan->listing || !out) return -1;
    Listing *L = scan->listing;
    if (atomic_load(&L->cancel)) return -1;
    int state = atomic_load(&L->state);
    if (state == LST_ERROR) return -1;
    if (state < LST_COMPLETE) return 0;

    OrderOpts oo = {engine_sort_key(scan->options.key), scan->options.desc,
                    scan->options.show_hidden, true, NULL, 0, 0, NULL, false};
    u32 *order = NULL;
    u32 n = 0;
    listing_build_order_ex(L, &oo, &order, &n);
    hermes_listing_free(out);
    out->items = xcalloc(n ? n : 1, sizeof *out->items);
    out->count = n;
    for (u32 i = 0; i < n; i++) {
        const Entry *e = &L->entries[order[i]];
        HermesEntry *copy = &out->items[i];
        copy->name = strdup(e->name);
        copy->name_len = e->name_len;
        copy->flags = e->flags;
        if (e->flags & ENT_DIR) {
            bool size_known;
            (void)entry_dir_size(e, &size_known);
            if (!size_known) copy->flags |= HERMES_ENTRY_SIZE_UNKNOWN;
        }
        copy->size = e->size;
        copy->mtime = e->mtime;
    }
    free(order);
    return 1;
}

int hermes_core_scan_start_folder_sizes(HermesCore *core, HermesScan *scan) {
    if (!core_live(core) || !scan || !scan->listing) return -1;
    Listing *L = scan->listing;
    if (atomic_load(&L->state) != LST_COMPLETE) return -1;
    if (L->dirsize_started) return 0;

    bool visible = true;
    bool hidden = scan->options.show_hidden;
    L->dirsize_started = true;
    L->dirsize_hidden = hidden;
    dirsize_start(L, visible, hidden, false, "", "", "");
    return 0;
}

void hermes_core_scan_cancel(HermesScan *scan) {
    if (scan && scan->listing) atomic_store(&scan->listing->cancel, 1);
}

void hermes_core_scan_free(HermesScan *scan) {
    if (!scan) return;
    listing_release(scan->listing);
    free(scan);
}

int hermes_core_scan_sorted(HermesCore *core, const char *path, HermesSortKey key, bool desc, HermesListing *out) {
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    HermesScanOptions options = {key, desc, true, false};
    HermesScan *scan = hermes_core_scan_start(core, path, &options);
    if (!scan) return -1;

    int result = 0;
    struct timespec pause = {0, 1000000};
    for (int i = 0; i < 10000; i++) {
        result = hermes_core_scan_poll(core, scan, out);
        if (result != 0) break;
        main_drain();
        nanosleep(&pause, NULL);
    }
    if (result == 0) result = hermes_core_scan_poll(core, scan, out);
    hermes_core_scan_free(scan);
    return result == 1 ? 0 : -1;
}

int hermes_core_scan(HermesCore *core, const char *path, HermesListing *out) {
    return hermes_core_scan_sorted(core, path, HERMES_SORT_NAME, false, out);
}

void hermes_listing_free(HermesListing *listing) {
    if (!listing) return;
    for (size_t i = 0; i < listing->count; i++) free((void *)listing->items[i].name);
    free(listing->items);
    memset(listing, 0, sizeof *listing);
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hermes_core_uri_to_path(const char *uri, char *out, size_t out_len) {
    static const char prefix[] = "file://";
    if (!uri || !out || !out_len) return -1;
    if (strncmp(uri, prefix, sizeof prefix - 1)) return -1;
    const char *p = uri + sizeof prefix - 1;
    // Portals send file:///path; a non-empty authority is not a local path.
    if (*p != '/') return -1;
    size_t o = 0;
    for (; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '%') {
            int hi = hex_nibble(p[1]);
            if (hi < 0) return -1;
            int lo = hex_nibble(p[2]);
            if (lo < 0) return -1;
            c = (unsigned char)(hi * 16 + lo);
            if (!c) return -1; // an encoded NUL cannot be part of a path
            p += 2;
        }
        if (o + 1 >= out_len) return -1; // leave room for the terminator
        out[o++] = (char)c;
    }
    out[o] = '\0';
    return 0;
}

int hermes_core_open_uris(HermesCore *core, const char *const *uris, size_t count, bool focus) {
    // Only the handle check needs the registry lock; never call the host under
    // it, since callbacks may re-enter the core.
    if (!core_live(core) || (count && !uris) || count > INT_MAX) return -1;
    char **paths = count ? calloc(count, sizeof *paths) : NULL;
    if (count && !paths) return -1;
    for (size_t i = 0; i < count; i++) {
        if (!uris[i]) goto fail;
        size_t n = strlen(uris[i]);
        paths[i] = malloc(n + 1); // decoding never grows the URI
        if (!paths[i]) goto fail;
        if (hermes_core_uri_to_path(uris[i], paths[i], n + 1) != 0) goto fail;
    }
    const HermesCoreHost *host = g_core_host;
    for (size_t i = 0; i < count; i++) host->open_tab(paths[i], -1, focus);
    for (size_t i = 0; i < count; i++) free(paths[i]);
    free(paths);
    return (int)count;
fail:
    for (size_t i = 0; i < count; i++) free(paths[i]);
    free(paths);
    return -1;
}

// --- engine operations: one job per ABI call, completion via the host hook ----

// The engine's internal OpKind numbering differs from the public HermesOpKind
// numbering; map the public one the engine understands.
static int op_kind_engine(HermesOpKind kind) {
    switch (kind) {
    case HERMES_OP_MKDIR: return OP_MKDIR;
    case HERMES_OP_RENAME: return OP_RENAME;
    case HERMES_OP_TRASH: return OP_TRASH;
    case HERMES_OP_DELETE: return OP_DELETE;
    case HERMES_OP_COPY: return OP_COPY;
    case HERMES_OP_MOVE: return OP_MOVE;
    }
    return OP_MKDIR; // not reached: the enum is closed
}

// The headless shell never asks a question, so every launch carries a concrete
// conflict policy; the engine must never be left waiting on CF_ASK.
static int policy_engine(HermesConflictPolicy policy) {
    switch (policy) {
    case HERMES_CONFLICT_SKIP: return CF_SKIP;
    case HERMES_CONFLICT_REPLACE: return CF_REPLACE;
    case HERMES_CONFLICT_RENAME: return CF_RENAME;
    case HERMES_CONFLICT_KEEP_BOTH: return CF_KEEP_BOTH;
    case HERMES_CONFLICT_NEWER: return CF_NEWER;
    }
    return -1;
}

// Hand one operation to the engine. Operations are asynchronous: the engine
// reports completion through its main-thread callback, which the host installs
// via HermesCoreHost.operations_changed.
static int launch_op(HermesCore *core, HermesOpKind kind, char **src, int nsrc, const char *dst_dir, char **dst, HermesConflictPolicy policy) {
    if (!core_live(core) || nsrc < 1 || (size_t)nsrc > (size_t)INT_MAX) return -1;
    for (int i = 0; i < nsrc; i++) if (!src[i] || !src[i][0]) return -1;
    int engine_policy = policy_engine(policy);
    if (engine_policy < 0) return -1;
    Op *op = op_new((int)op_kind_engine(kind), src, nsrc, dst_dir);
    if (dst) op->dst = strv_dup(dst, nsrc);
    op->policy = engine_policy;
    op_launch(op);
    return 0;
}

int hermes_core_mkdir(HermesCore *core, const char *parent, const char *name) {
    if (!core_live(core) || !parent || !parent[0] || !name || !name[0]) return -1;
    char joined[PATH_MAX];
    if ((int)snprintf(joined, sizeof joined, "%s/%s", parent, name) >= (int)sizeof joined) return -1;
    char *srcs[1];
    srcs[0] = strdup(joined);
    int rc = launch_op(core, HERMES_OP_MKDIR, srcs, 1, NULL, NULL, HERMES_CONFLICT_RENAME);
    free(srcs[0]);
    return rc;
}

int hermes_core_rename(HermesCore *core, const char *from, const char *to) {
    if (!core_live(core) || !from || !from[0] || !to || !to[0]) return -1;
    char *srcs[1];
    char *dsts[1];
    srcs[0] = strdup(from);
    dsts[0] = strdup(to);
    int rc = launch_op(core, HERMES_OP_RENAME, srcs, 1, NULL, dsts, HERMES_CONFLICT_RENAME);
    free(srcs[0]);
    free(dsts[0]);
    return rc;
}

int hermes_core_remove(HermesCore *core, const char *const *paths, size_t count, bool trash) {
    if (!core_live(core) || count < 1 || !paths || (size_t)count > (size_t)INT_MAX) return -1;
    char **src = calloc(count, sizeof *src);
    if (!src) return -1;
    for (size_t i = 0; i < count; i++) {
        if (!paths[i] || !paths[i][0]) {
            for (size_t k = 0; k < count; k++) free(src[k]);
            free(src);
            return -1;
        }
        src[i] = strdup(paths[i]);
    }
    int rc = launch_op(core, trash ? HERMES_OP_TRASH : HERMES_OP_DELETE, src, (int)count, NULL, NULL, HERMES_CONFLICT_SKIP);
    for (size_t i = 0; i < count; i++) free(src[i]);
    free(src);
    return rc;
}

int hermes_core_copy(HermesCore *core, const char *const *paths, size_t count, const char *dest_dir, bool move, HermesConflictPolicy policy) {
    if (!core_live(core) || count < 1 || !paths || !dest_dir || !dest_dir[0] || (size_t)count > (size_t)INT_MAX) return -1;
    char **src = calloc(count, sizeof *src);
    if (!src) return -1;
    for (size_t i = 0; i < count; i++) {
        if (!paths[i] || !paths[i][0]) {
            for (size_t k = 0; k < count; k++) free(src[k]);
            free(src);
            return -1;
        }
        src[i] = strdup(paths[i]);
    }
    int rc = launch_op(core, move ? HERMES_OP_MOVE : HERMES_OP_COPY, src, (int)count, dest_dir, NULL, policy);
    for (size_t i = 0; i < count; i++) free(src[i]);
    free(src);
    return rc;
}
