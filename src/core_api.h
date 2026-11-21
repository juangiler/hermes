// core_api.h - public C ABI for the headless Hermes engine.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HermesCore HermesCore;
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
int hermes_core_scan(HermesCore *core, const char *path, HermesListing *out);
void hermes_listing_free(HermesListing *listing);

#ifdef __cplusplus
}
#endif
