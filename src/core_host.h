// core_host.h - engine-to-host callbacks. The host installs its own table at
// startup; with the defaults the engine stays headless and side-effect free.
#pragma once
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HermesCoreHost {
    bool (*is_selftest)(void);
    bool (*is_screenshot)(void);
    bool (*window_hidden)(void);
    void (*mark_settings_dirty)(void);
    void (*request_redraw)(void);
    void (*open_tab)(const char *path, int index, bool focus);
    int (*current_tab)(void);
} HermesCoreHost;

static bool core_host_false(void) { return false; }
static int core_host_zero(void) { return 0; }
static void core_host_noop(void) {}
static void core_host_open_tab_noop(const char *path, int index, bool focus) {
    (void)path;
    (void)index;
    (void)focus;
}

static const HermesCoreHost core_host_defaults = {
    core_host_false,
    core_host_false,
    core_host_false,
    core_host_noop,
    core_host_noop,
    core_host_open_tab_noop,
    core_host_zero,
};

// Not every translation unit that needs the struct installs the hook table;
// the inert defaults keep those uses headless and side-effect free.
static const HermesCoreHost *g_core_host __attribute__((unused)) = &core_host_defaults;

#ifdef __cplusplus
}
#endif
