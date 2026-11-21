// hermes_core.c - unity source for the headless hermes_core C library.
// Compiled only into the core library; the SDL app keeps using src/main.c.

#include "base.h"
#include "core_api.h"
#include "core_host.h"
#include <ctype.h>
#include <dlfcn.h>
#include <locale.h>
#include <regex.h>

#include "pool.c"
#include "ipc.c"
#include "fs.c"
#include "local_shares.c"
#include "ops.c"
#include "archive.c"
#include "search.c"
#include "diskuse.c"
#include "compare.c"
#include "snapshots.c"
#include "svg_preview.c"
#include "preview.c"
#include "usb.c"
#include "plist.c"
#include "iphone.c"
#include "mtp.c"
#include "devfs.c"
#include "devices.c"
#include "git.c"
#include "apps.c"
#include "cloud_identity.c"

// The engine files forward-declare these host callbacks; the headless library
// supplies inert defaults. A real frontend supplies its own in its own TU.
static void op_finished(void *arg) { op_free((Op *)arg); }
static void search_batch_cb(void *arg) { (void)arg; }
static void content_batch_cb(void *arg) { (void)arg; }
static void dirsize_done_cb(void *arg) { (void)arg; }
static void du_scan_exit_cb(void *arg) { (void)arg; }
static void cmp_job_done_cb(void *arg) { (void)arg; }
static void preview_done_cb(void *arg) { (void)arg; }
static void thumb_done_cb(void *arg) { (void)arg; }
static void dev_job_done(void *arg) { (void)arg; }
static void dev_phone_wait(void *arg) { free(arg); }
static void dev_net_question(void *arg) { free(arg); }
static bool app_selftest(void) { return g_core_host->is_selftest(); }
static bool ssh_target_uri(const char *uri, SshTarget *t) {
    (void)uri;
    if (t) memset(t, 0, sizeof *t);
    return false;
}
static void git_resolve_done(void *arg) { (void)arg; }
static void git_job_done(void *arg) { (void)arg; }
static void git_subs_done(void *arg) { (void)arg; }
static void spawn_terminal(const char *dir, char *const cmd[]) {
    (void)dir;
    (void)cmd;
}

#include "core_api.c"
