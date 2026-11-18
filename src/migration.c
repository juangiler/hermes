// Recoverable, conservative import. Sources are never changed. Directory-relative
// O_NOFOLLOW opens keep both traversal and writes away from symlink targets.
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
typedef struct { int imported, conflicts, excluded, failed; } MigrationResult;
static int g_migration_lock = -1;

static void migration_key(const char *name, char *out, size_t cap) {
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)name; *p && n + 2 < cap; p++)
    n += (size_t)snprintf(out + n, cap - n, "%02x", *p);
  out[n] = 0;
}
static int migration_record(int ledger, const char *key, char outcome) {
  char line[1024];
  int n = snprintf(line, sizeof line, "%c %s\n", outcome, key);
  return write(ledger, line, (size_t)n) == n && fsync(ledger) == 0;
}
static MigrationResult migration_import(int source, int dest, bool retry, bool data_root) {
  MigrationResult result = {0};
  int ledger = openat(dest, ".hermes-import-ledger", O_RDWR | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (ledger < 0) { result.failed++; return result; }
  FILE *records = fdopen(dup(ledger), "r");
  if (!records) { close(ledger); result.failed++; return result; }
  DIR *dir = fdopendir(dup(source));
  if (!dir) { fclose(records); close(ledger); result.failed++; return result; }
  rewinddir(dir);
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    const char *name = entry->d_name;
    if (!strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, ".hermes-import-ledger")) continue;
    char key[512], line[1024];
    migration_key(name, key, sizeof key);
    rewind(records);
    bool done = false, failed = false;
    while (fgets(line, sizeof line, records)) {
      line[strcspn(line, "\n")] = 0;
      if (strlen(line) > 2 && !strcmp(line + 2, key)) {
        failed = line[0] == 'F';
        done = !failed;
      }
    }
    struct stat st, dst;
    if (fstatat(source, name, &st, AT_SYMLINK_NOFOLLOW)) { result.failed++; continue; }
    if (done) continue;
    if (failed && !retry) { result.failed++; continue; }
    bool terminal = false;
    char outcome = 'I';
    int previous_failures = result.failed;
    if ((data_root && !strcmp(name, "cloud")) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
      if (!done) result.excluded++;
      outcome = 'E';
      terminal = true;
    } else if (S_ISDIR(st.st_mode)) {
      int sd = openat(source, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      bool exists = fstatat(dest, name, &dst, AT_SYMLINK_NOFOLLOW) == 0;
      if (exists && !S_ISDIR(dst.st_mode)) { if (!done) result.conflicts++; terminal = true; outcome = 'C'; }
      else if (sd < 0 || (!exists && mkdirat(dest, name, 0700))) result.failed++;
      else {
        int dd = openat(dest, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (dd < 0) result.failed++;
        else {
          MigrationResult sub = migration_import(sd, dd, retry, false);
          result.imported += sub.imported; result.conflicts += sub.conflicts;
          result.excluded += sub.excluded; result.failed += sub.failed;
          close(dd);
        }
      }
      if (sd >= 0) close(sd);
      if (result.failed == previous_failures) terminal = true;
    } else if (done) continue;
    else if (fstatat(dest, name, &dst, AT_SYMLINK_NOFOLLOW) == 0) {
      result.conflicts++; terminal = true; outcome = 'C';
    } else {
      int in = openat(source, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      char tmp[80];
      snprintf(tmp, sizeof tmp, ".hermes-import-%u-%lu", (unsigned)getpid(), (unsigned long)now_ns());
      int out = openat(dest, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      bool ok = in >= 0 && out >= 0;
      char buf[65536];
      ssize_t n;
      while (ok && (n = read(in, buf, sizeof buf)) != 0) {
        if (n < 0) { if (errno == EINTR) continue; ok = false; break; }
        for (ssize_t off = 0; ok && off < n;) {
          ssize_t w = write(out, buf + off, (size_t)(n - off));
          if (w < 0 && errno == EINTR) continue;
          if (w <= 0) ok = false;
          else off += w;
        }
      }
      if (ok) ok = fsync(out) == 0;
      if (in >= 0) close(in);
      if (out >= 0) close(out);
      // linkat is an atomic no-replace commit, even when another writer races us.
      if (ok) ok = linkat(dest, tmp, dest, name, 0) == 0;
      unlinkat(dest, tmp, 0);
      if (ok) { result.imported++; terminal = true; }
      else result.failed++;
    }
    if (result.failed > previous_failures) {
      if (!migration_record(ledger, key, 'F')) result.failed++;
    } else if (terminal && !done && !migration_record(ledger, key, outcome)) result.failed++;
  }
  fsync(dest);
  closedir(dir); fclose(records); close(ledger);
  return result;
}

// Private roots are created one component at a time without following symlinks.
static int migration_private_dir(const char *path) {
  if (path[0] != '/') return -1;
  int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  char copy[PATH_MAX];
  snprintf(copy, sizeof copy, "%s", path);
  char *p = copy, *part;
  while (fd >= 0 && (part = strsep(&p, "/"))) {
    if (!*part) continue;
    if (!strcmp(part, ".") || !strcmp(part, "..")) { close(fd); return -1; }
    if (mkdirat(fd, part, 0700) && errno != EEXIST) { close(fd); return -1; }
    int next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(fd); fd = next;
  }
  struct stat st;
  if (fd >= 0 && (fstat(fd, &st) || st.st_uid != getuid() || (st.st_mode & 0077))) {
    close(fd); return -1;
  }
  return fd;
}

static bool migration_startup(void) {
  char root[PATH_MAX];
  identity_xdg("XDG_STATE_HOME", ".local/state", HERMES_SLUG, root, sizeof root);
  int state = migration_private_dir(root);
  if (state < 0) return false;
  g_migration_lock = openat(state, "startup.lock", O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
  close(state);
  if (g_migration_lock < 0 || flock(g_migration_lock, LOCK_EX | LOCK_NB)) return false;
  struct sockaddr_un a = {.sun_family = AF_UNIX};
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (runtime && runtime[0] == '/') snprintf(a.sun_path, sizeof a.sun_path, "%s/file-explorer.sock", runtime);
  else snprintf(a.sun_path, sizeof a.sun_path, "/tmp/file-explorer-%u.sock", (unsigned)getuid());
  int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe < 0) return false;
  bool live = connect(probe, (struct sockaddr *)&a, sizeof a) == 0;
  close(probe);
  if (live) { fprintf(stderr, "Hermes: close the running File Explorer before migration.\n"); return false; }
  const char *vars[] = {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"};
  const char *fallback[] = {".config", ".local/share", ".cache", ".local/state"};
  bool ok = true;
  for (int i = 0; i < 4; i++) {
    char legacy[PATH_MAX], current[PATH_MAX];
    identity_xdg(vars[i], fallback[i], HERMES_LEGACY_SLUG, legacy, sizeof legacy);
    identity_xdg(vars[i], fallback[i], HERMES_SLUG, current, sizeof current);
    int src = open(legacy, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (src < 0) { if (errno != ENOENT) ok = false; continue; }
    int dst = migration_private_dir(current);
    if (dst < 0) { close(src); ok = false; continue; }
    MigrationResult r = migration_import(src, dst, getenv("HERMES_MIGRATION_RETRY") != NULL, i == 1);
    fprintf(stderr, "Hermes migration (%s): %d imported, %d conflicts, %d excluded, %d failed; sources retained.\n",
            vars[i], r.imported, r.conflicts, r.excluded, r.failed);
    ok &= !r.failed;
    close(src); close(dst);
  }
  return ok;
}

static void migration_finish(void) {
  if (g_migration_lock < 0) return;
  flock(g_migration_lock, LOCK_UN);
  close(g_migration_lock); g_migration_lock = -1;
}
