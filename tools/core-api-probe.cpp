// Standalone C++17 consumer check for the hermes_core C ABI.
// Compiled by CMake against the hermes_core static library, not the harness
// unity source, so it proves the archive a Qt frontend would link is usable.
#include "core_api.h"
#include "core_host.h"
#include <cstdio>

static bool probe_false() { return false; }
static int probe_zero() { return 0; }
static void probe_noop() {}
static void probe_open_tab(const char *path, int index, bool focus) {
    (void)path;
    (void)index;
    (void)focus;
}

static const HermesCoreHost probe_host = {
    probe_false, probe_false, probe_false,
    probe_noop, probe_noop, probe_open_tab, probe_zero,
};

int main() {
    hermes_core_set_host(&probe_host);
    HermesCore *core = hermes_core_create(nullptr, nullptr);
    if (!core) return 1;
    hermes_core_drain(core);
    hermes_core_destroy(core);
    std::puts("CPP ABI PROBE OK");
    return 0;
}
