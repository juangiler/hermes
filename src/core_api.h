// core_api.h - public C ABI for the headless Hermes engine.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HermesCore HermesCore;
typedef struct HermesScan HermesScan;
typedef void (*HermesWakeFn)(void *user);

typedef struct {
    const char *name;
    size_t name_len;
    unsigned char flags;
    uint64_t size;
    int64_t mtime;
} HermesEntry;

typedef struct {
    HermesEntry *items;
    size_t count;
} HermesListing;

struct HermesCoreHost; // defined in core_host.h; installed once by the frontend

HermesCore *hermes_core_create(HermesWakeFn wake, void *user);
// The table is copied, but install it before starting any jobs.
void hermes_core_set_host(const struct HermesCoreHost *host);
void hermes_core_destroy(HermesCore *core);
void hermes_core_drain(HermesCore *core);
// Convert an incoming file:// URI to a local path; 0 on success, -1 otherwise.
int hermes_core_uri_to_path(const char *uri, char *out, size_t out_len);
// Validate the URI batch, then hand each path to the host's open_tab with
// index -1. Returns the number opened, or -1 if the core or any URI is invalid.
int hermes_core_open_uris(HermesCore *core, const char *const *uris, size_t count, bool focus);

// Sort orders for the headless listing ABI. Hermes_SORT_* map onto the engine's
// SORT_* keys; the headless shell honours hidden-entry and folder-size options.
typedef enum {
    HERMES_SORT_NAME = 0, HERMES_SORT_DATE = 1,
    HERMES_SORT_TYPE = 2, HERMES_SORT_SIZE = 3,
    HERMES_SORT_CREATED = 4, HERMES_SORT_EXT = 5,
    HERMES_SORT_PERMS = 6, HERMES_SORT_OWNER = 7
} HermesSortKey;

typedef struct {
    HermesSortKey key;
    bool desc;
    bool show_hidden;
    bool folder_sizes;
} HermesScanOptions;

// Conflict policies for the headless operation ABI; the headless shell never
// asks a question, so a concrete policy must always be given (never CF_ASK).
typedef enum {
    HERMES_CONFLICT_SKIP = 0, HERMES_CONFLICT_REPLACE = 1,
    HERMES_CONFLICT_RENAME = 2, HERMES_CONFLICT_KEEP_BOTH = 3,
    HERMES_CONFLICT_NEWER = 4
} HermesConflictPolicy;

typedef enum {
    HERMES_OP_MKDIR = 0, HERMES_OP_RENAME = 1, HERMES_OP_TRASH = 2,
    HERMES_OP_DELETE = 3, HERMES_OP_COPY = 4, HERMES_OP_MOVE = 5
} HermesOpKind;

enum {
    HERMES_ENTRY_DIR = 1,
    HERMES_ENTRY_LINK = 2,
    HERMES_ENTRY_HIDDEN = 4,
    HERMES_ENTRY_SIZE_UNKNOWN = 8
};

int hermes_core_scan(HermesCore *core, const char *path, HermesListing *out);
void hermes_listing_free(HermesListing *listing);
HermesScan *hermes_core_scan_start(HermesCore *core, const char *path,
                                   const HermesScanOptions *options);
// `out` must be zero-initialized before the first poll; completed polls replace it.
int hermes_core_scan_poll(HermesCore *core, HermesScan *scan, HermesListing *out);
void hermes_core_scan_cancel(HermesScan *scan);
void hermes_core_scan_free(HermesScan *scan);
int hermes_core_scan_sorted(HermesCore *core, const char *path, HermesSortKey key, bool desc, HermesListing *out);
int hermes_core_mkdir(HermesCore *core, const char *parent, const char *name);
int hermes_core_rename(HermesCore *core, const char *from, const char *to);
int hermes_core_remove(HermesCore *core, const char *const *paths, size_t count, bool trash);
int hermes_core_copy(HermesCore *core, const char *const *paths, size_t count, const char *dest_dir, bool move, HermesConflictPolicy policy);

#ifdef __cplusplus
}
#endif
