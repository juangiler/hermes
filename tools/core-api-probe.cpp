// Standalone C++17 consumer check for the hermes_core C ABI.
// Compiled by CMake against the hermes_core static library, not the harness
// unity source, so it proves the archive a Qt frontend would link is usable.
#include "core_api.h"
#include "core_host.h"
#include <cstdio>

static bool probe_false() { return false; }
static int probe_zero() { return 0; }
static void probe_noop() {}
static void probe_noop_op(int kind, int error_count) { (void)kind; (void)error_count; }
static void probe_open_tab(const char *path, int index, bool focus) {
    (void)path;
    (void)index;
    (void)focus;
}

static const HermesCoreHost probe_host = {
    probe_false, probe_false, probe_false,
    probe_noop, probe_noop, probe_open_tab, probe_zero,
    probe_noop_op,
};

int main() {
    hermes_core_set_host(&probe_host);
    HermesCore *core = hermes_core_create(nullptr, nullptr);
    if (!core) return 1;
    // Take the address of every new entry point: this is the C++ side of the
    // extern "C" linkage the Qt frontend compiles against.
    auto scan_sorted = &hermes_core_scan_sorted;
    auto mkdir = &hermes_core_mkdir;
    auto rename = &hermes_core_rename;
    auto remove = &hermes_core_remove;
    auto copy = &hermes_core_copy;
    if (scan_sorted == nullptr || mkdir == nullptr || rename == nullptr || remove == nullptr || copy == nullptr)
        return 1;
    if (scan_sorted(core, "", HERMES_SORT_NAME, false, nullptr) != -1) return 1;
    if (mkdir(core, "", "") != -1) return 1;
    if (rename(core, "", "") != -1) return 1;
    if (remove(core, nullptr, 0, false) != -1) return 1;
    if (copy(core, nullptr, 0, "", false, HERMES_CONFLICT_SKIP) != -1) return 1;
    hermes_core_drain(core);
    hermes_core_destroy(core);
    std::puts("CPP ABI PROBE OK");
    return 0;
}
