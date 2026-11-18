// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/identity-selftest.c -o /tmp/opencode/hermes-identity-test -pthread -lm
#include "../src/base.h"
#include "../src/migration.c"
#include "../src/integration_identity.c"
#include <assert.h>

static void fixture_write(int dir, const char *name, const char *text) {
  int fd = openat(dir, name, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  assert(fd >= 0);
  assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
  close(fd);
}
// settings.c's file_read_all is not in this translation unit.
static char *read_text(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
  char *buf = malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[got] = 0;
  return buf;
}
int main(void) {
  assert(!strcmp(HERMES_APP_ID, "io.github.juangiler.hermes"));
  setenv("FE_FIXTURE", "old", 1);
  assert(!strcmp(identity_env("FE_FIXTURE"), "old"));
  setenv("HERMES_FIXTURE", "new", 1);
  assert(!strcmp(identity_env("FE_FIXTURE"), "new"));
  char root[] = "/tmp/opencode/hermes-identity-XXXXXX";
  assert(mkdtemp(root));
  int r = open(root, O_DIRECTORY | O_RDONLY);
  assert(mkdirat(r, "legacy", 0700) == 0 && mkdirat(r, "hermes", 0700) == 0);
  int s = openat(r, "legacy", O_DIRECTORY | O_RDONLY);
  int d = openat(r, "hermes", O_DIRECTORY | O_RDONLY);
  fixture_write(s, "settings.conf", "legacy");
  fixture_write(s, "conflict", "old");
  fixture_write(d, "conflict", "new");
  assert(mkdirat(s, "cloud", 0700) == 0);
  assert(symlinkat("/etc/passwd", s, "unsafe") == 0);
  assert(symlinkat("/tmp", d, "escape") == 0);
  assert(mkdirat(s, "escape", 0700) == 0);
  int e = openat(s, "escape", O_DIRECTORY | O_RDONLY);
  fixture_write(e, "no-write", "secret");
  close(e);
  MigrationResult m = migration_import(s, d, false, true);
  assert(m.imported == 1 && m.conflicts == 2 && m.excluded == 2 && !m.failed);
  struct stat st;
  assert(fstatat(d, "settings.conf", &st, 0) == 0 && (st.st_mode & 0777) == 0600);
  assert(fstatat(d, "cloud", &st, AT_SYMLINK_NOFOLLOW) < 0);
  assert(fstatat(d, "unsafe", &st, AT_SYMLINK_NOFOLLOW) < 0);
  assert(unlinkat(d, "settings.conf", 0) == 0);
  m = migration_import(s, d, false, true);
  assert(!m.imported && !m.failed);
  assert(fstatat(d, "settings.conf", &st, 0) < 0);
  assert(fstatat(s, "settings.conf", &st, 0) == 0);
  fixture_write(s, "unreadable", "retry");
  assert(fchmodat(s, "unreadable", 0000, 0) == 0);
  m = migration_import(s, d, false, true);
  assert(m.failed == 1);
  assert(fchmodat(s, "unreadable", 0600, 0) == 0);
  m = migration_import(s, d, false, true);
  assert(m.failed == 1 && m.imported == 0); // retry must be explicit
  m = migration_import(s, d, true, true);
  assert(m.failed == 0 && m.imported == 1);
  char xdg[PATH_MAX];
  setenv("XDG_CONFIG_HOME", "relative-is-ignored", 1);
  setenv("HOME", root, 1);
  identity_xdg("XDG_CONFIG_HOME", ".config", HERMES_SLUG, xdg, sizeof xdg);
  char expected[PATH_MAX];
  snprintf(expected, sizeof expected, "%s/.config/hermes", root);
  assert(!strcmp(xdg, expected));
  setenv("XDG_CONFIG_HOME", root, 1);
  identity_xdg("XDG_CONFIG_HOME", ".config", HERMES_SLUG, xdg, sizeof xdg);
  snprintf(expected, sizeof expected, "%s/hermes", root);
  assert(!strcmp(xdg, expected));
  close(s); close(d); close(r);

  // --- startup: the legacy listener probe and the startup lock ---
  char su[] = "/tmp/opencode/hermes-startup-XXXXXX";
  assert(mkdtemp(su));
  char rt[PATH_MAX], cfg[PATH_MAX], dat[PATH_MAX], cac[PATH_MAX], sta[PATH_MAX];
  snprintf(rt, sizeof rt, "%s/rt", su);
  snprintf(cfg, sizeof cfg, "%s/cfg", su);
  snprintf(dat, sizeof dat, "%s/data", su);
  snprintf(cac, sizeof cac, "%s/cache", su);
  snprintf(sta, sizeof sta, "%s/state", su);
  assert(!mkdir(rt, 0700) && !mkdir(cfg, 0700) && !mkdir(dat, 0700) && !mkdir(cac, 0700) && !mkdir(sta, 0700));
  char legcfg[PATH_MAX];
  snprintf(legcfg, sizeof legcfg, "%s/file-explorer", cfg);
  assert(!mkdir(legcfg, 0700));
  {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/settings.conf", legcfg);
    int fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    assert(fd >= 0 && write(fd, "legacy-settings", 15) == 15);
    close(fd);
  }
  setenv("XDG_RUNTIME_DIR", rt, 1);
  setenv("XDG_CONFIG_HOME", cfg, 1);
  setenv("XDG_DATA_HOME", dat, 1);
  setenv("XDG_CACHE_HOME", cac, 1);
  setenv("XDG_STATE_HOME", sta, 1);
  char sock[PATH_MAX];
  snprintf(sock, sizeof sock, "%s/file-explorer.sock", rt);
  // (a) a live legacy instance blocks startup before anything is imported
  int ls = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(ls >= 0);
  struct sockaddr_un la = {.sun_family = AF_UNIX};
  snprintf(la.sun_path, sizeof la.sun_path, "%s", sock);
  assert(bind(ls, (struct sockaddr *)&la, sizeof la) == 0 && listen(ls, 1) == 0);
  assert(!migration_startup());
  migration_finish();
  {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/hermes", cfg);
    assert(access(p, F_OK) != 0); // the import never ran
  }
  close(ls);
  unlink(sock);
  // (b) another holder of the startup lock blocks startup
  char lockp[PATH_MAX], herst[PATH_MAX];
  snprintf(herst, sizeof herst, "%s/hermes", sta);
  snprintf(lockp, sizeof lockp, "%s/startup.lock", herst);
  int lfd = open(lockp, O_CREAT | O_RDWR, 0600);
  assert(lfd >= 0 && flock(lfd, LOCK_EX | LOCK_NB) == 0);
  assert(!migration_startup());
  migration_finish();
  {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/hermes", cfg);
    assert(access(p, F_OK) != 0); // still no import
  }
  flock(lfd, LOCK_UN);
  close(lfd);
  // (c) clean start: import runs, sources are retained, the lock is released
  assert(migration_startup());
  migration_finish();
  {
    char dst[PATH_MAX];
    snprintf(dst, sizeof dst, "%s/hermes/settings.conf", cfg);
    char *text = read_text(dst);
    assert(text && !strcmp(text, "legacy-settings"));
    free(text);
    snprintf(dst, sizeof dst, "%s/file-explorer/settings.conf", cfg);
    assert(access(dst, F_OK) == 0); // source retained
    snprintf(dst, sizeof dst, "%s/hermes/.hermes-import-ledger", cfg);
    assert(access(dst, F_OK) == 0); // per-file ledger
    int tfd = open(lockp, O_RDWR);
    assert(tfd >= 0 && flock(tfd, LOCK_EX | LOCK_NB) == 0); // released
    flock(tfd, LOCK_UN);
    close(tfd);
    // idempotent: a destination that exists wins and is not re-imported over
    snprintf(dst, sizeof dst, "%s/hermes/settings.conf", cfg);
    int fd = open(dst, O_WRONLY | O_TRUNC);
    assert(fd >= 0 && write(fd, "hermes-wins", 11) == 11);
    close(fd);
  }
  assert(migration_startup());
  migration_finish();
  {
    char dst[PATH_MAX];
    snprintf(dst, sizeof dst, "%s/hermes/settings.conf", cfg);
    char *text = read_text(dst);
    assert(text && !strcmp(text, "hermes-wins"));
    free(text);
    snprintf(dst, sizeof dst, "%s/file-explorer/settings.conf", cfg);
    text = read_text(dst);
    assert(text && !strcmp(text, "legacy-settings"));
    free(text);
  }

  // --- generated-integration provenance: only recognizable app output is adopted ---
  {
    const char *gen = "[Desktop Entry]\nType=Application\nName=File Explorer\nExec=file-explorer %F\n"
                      "Icon=dev.fileexplorer\nTerminal=false\nStartupWMClass=dev.fileexplorer\n";
    char *out = identity_legacy_desktop(gen, "/opt/Hermes/bin/hermes");
    assert(out);
    assert(strstr(out, "Name=Hermes\n"));
    assert(strstr(out, "Icon=" HERMES_APP_ID ".desktop\n") == NULL); // Icon= has no .desktop suffix
    assert(strstr(out, "Icon=" HERMES_APP_ID "\n"));
    assert(strstr(out, "StartupWMClass=" HERMES_APP_ID "\n"));
    assert(strstr(out, "Exec=\"/opt/Hermes/bin/hermes\" %F\n"));
    free(out);
    // same file name, user's own launcher: never rewritten
    const char *custom = "[Desktop Entry]\nName=My Manager\nExec=other %F\n"
                         "Icon=dev.fileexplorer\nStartupWMClass=dev.fileexplorer\n";
    assert(!identity_legacy_desktop(custom, "/opt/Hermes/bin/hermes"));
    // a path that cannot be written safely into a desktop entry is refused
    assert(!identity_legacy_desktop(gen, "/opt/has\"quote"));

  }
  puts("identity/migration fixtures passed");
  return 0;
}
