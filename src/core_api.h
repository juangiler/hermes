// core_api.h - public C ABI for the headless Hermes engine.
#pragma once
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

HermesCore *hermes_core_create(HermesWakeFn wake, void *user);
void hermes_core_destroy(HermesCore *core);
void hermes_core_drain(HermesCore *core);
int hermes_core_scan(HermesCore *core, const char *path, HermesListing *out);
void hermes_listing_free(HermesListing *listing);

#ifdef __cplusplus
}
#endif
