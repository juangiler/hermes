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
    void (*operations_changed)(int kind, int error_count); // kind = HermesOpKind
} HermesCoreHost;

static bool core_host_false(void) { return false; }
static int core_host_zero(void) { return 0; }
static void core_host_noop(void) {}
static void core_host_noop_op(int kind, int error_count) { (void)kind; (void)error_count; }
static void core_host_open_tab_noop(const char *path, int index, bool focus) {
    (void)path;
    (void)index;
    (void)focus;
}

static const HermesCoreHost hermes_core_host_defaults = {
    core_host_false,
    core_host_false,
    core_host_false,
    core_host_noop,
    core_host_noop,
    core_host_open_tab_noop,
    core_host_zero,
    core_host_noop_op,
};

// Not every translation unit that needs the struct uses the hook table (for
// example a C++ consumer that only calls hermes_core_set_host()).
static const HermesCoreHost *g_core_host __attribute__((unused)) = &hermes_core_host_defaults;

#ifdef __cplusplus
}
#endif
