// Secret Service adapter: never export or enumerate credentials. A durable
// deletion intent blocks both namespaces until a verified explicit store.
static int secret_cmd(char *const argv[], const char *input, char *out, size_t cap) {
  int in[2], o[2], result[2];
  if (out && cap) out[0] = 0;
  if (pipe2(in, O_CLOEXEC)) return -1;
  if (pipe2(o, O_CLOEXEC)) { close(in[0]); close(in[1]); return -1; }
  if (pipe2(result, O_CLOEXEC)) { close(in[0]); close(in[1]); close(o[0]); close(o[1]); return -1; }
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in[0], 0);
  posix_spawn_file_actions_adddup2(&fa, o[1], 1);
  posix_spawn_file_actions_addclose(&fa, in[1]);
  posix_spawn_file_actions_addclose(&fa, o[0]);
  posix_spawn_file_actions_adddup2(&fa, result[1], 3);
  posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
  pid_t pid;
  // The app auto-reaps detached desktop helpers with SIGCHLD=SIG_IGN.
  // A shell wrapper reports the real helper's exit code privately instead of
  // changing process-global signal disposition (which would race workers).
  char *wrapped[64] = {"/bin/sh", "-c", "\"$@\"; rc=$?; printf '%s\\n' \"$rc\" >&3", "hermes-secret"};
  int argc = 0;
  while (argv[argc] && argc < 58) { wrapped[argc + 4] = argv[argc]; argc++; }
  wrapped[argc + 4] = NULL;
  int rc = posix_spawn(&pid, "/bin/sh", &fa, spawn_attr(), wrapped, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(in[0]); close(o[1]); close(result[1]);
  if (rc) { close(in[1]); close(o[0]); close(result[0]); return -1; }
  bool ok = true;
  if (input) {
    size_t n = strlen(input), off = 0;
    while (off < n) {
      ssize_t w = write(in[1], input + off, n - off);
      if (w < 0 && errno == EINTR) continue;
      if (w <= 0) { ok = false; break; }
      off += (size_t)w;
    }
  }
  close(in[1]);
  size_t n = 0;
  char tmp[512];
  for (;;) {
    ssize_t r = read(o[0], tmp, sizeof tmp);
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) ok = false;
    if (r <= 0) break;
    for (ssize_t i = 0; i < r && out; i++) {
      if (n + 1 < cap) out[n++] = tmp[i];
      else ok = false;
    }
  }
  explicit_bzero(tmp, sizeof tmp);
  close(o[0]);
  if (out && cap) out[n] = 0;
  char status[24] = "";
  ssize_t got;
  do { got = read(result[0], status, sizeof status - 1); } while (got < 0 && errno == EINTR);
  close(result[0]);
  int ignored;
  while (waitpid(pid, &ignored, 0) < 0 && errno == EINTR) {}
  if (!ok || got < 2 || strcmp(status, "0\n")) return -1;
  return (int)n;
}
static bool secret_available(void) {
  const char *t = identity_env("HERMES_SECRET_TOOL");
  return (t && t[0]) || access("/usr/bin/secret-tool", X_OK) == 0 || access("/usr/local/bin/secret-tool", X_OK) == 0;
}
// Private roots are created one component at a time without following symlinks.
static int secret_private_dir(const char *path) {
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
static int secret_intent_dir(void) {
  char path[PATH_MAX];
  identity_xdg("XDG_STATE_HOME", ".local/state", HERMES_SLUG "/credential-deletions", path, sizeof path);
  return secret_private_dir(path);
}
static void secret_intent_name(const char *uri, char *name, size_t cap) {
  // The marker is not a secret or a URI export. Two independent hashes avoid
  // path-length limits; a collision fails closed (never resurrect a password).
  u64 a = 1469598103934665603ull, b = 5381;
  for (const unsigned char *p = (const unsigned char *)uri; *p; p++) { a = (a ^ *p) * 1099511628211ull; b = b * 33 + *p; }
  snprintf(name, cap, "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
}
static bool secret_blocked(const char *uri) {
  int d = secret_intent_dir();
  if (d < 0) return true;
  char name[40]; secret_intent_name(uri, name, sizeof name);
  struct stat st;
  bool blocked = fstatat(d, name, &st, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT;
  close(d);
  return blocked;
}
static bool secret_lookup_namespace(const char *uri, const char *app, char *out, size_t cap) {
  char *argv[] = {(char *)tool("HERMES_SECRET_TOOL", "secret-tool"), "lookup", "application", (char *)app, "uri", (char *)uri, NULL};
  bool ok = secret_cmd(argv, NULL, out, cap) > 0 && out[0];
  if (ok) out[strcspn(out, "\r\n")] = 0;
  return ok && out[0];
}
static bool secret_store(const char *uri, const char *password) {
  char label[1100];
  snprintf(label, sizeof label, "Hermes: %s", uri);
  char *argv[] = {(char *)tool("HERMES_SECRET_TOOL", "secret-tool"), "store", "--label", label, "application", HERMES_SLUG, "uri", (char *)uri, NULL};
  if (secret_cmd(argv, password, NULL, 0) < 0) return false;
  char verified[4096];
  bool ok = secret_lookup_namespace(uri, HERMES_SLUG, verified, sizeof verified) && !strcmp(verified, password);
  explicit_bzero(verified, sizeof verified);
  if (ok) {
    int d = secret_intent_dir();
    if (d < 0) return false;
    char name[40]; secret_intent_name(uri, name, sizeof name);
    ok = unlinkat(d, name, 0) == 0 || errno == ENOENT;
    ok = fsync(d) == 0 && ok;
    close(d);
  }
  return ok;
}
static bool secret_lookup(const char *uri, char *out, size_t cap) {
  if (cap) out[0] = 0;
  if (secret_blocked(uri)) return false;
  return secret_lookup_namespace(uri, HERMES_SLUG, out, cap);
}
static bool secret_clear(const char *uri) {
  int d = secret_intent_dir();
  if (d < 0) return false;
  char name[40]; secret_intent_name(uri, name, sizeof name);
  int fd = openat(d, name, O_CREAT | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
  bool ok = fd >= 0 && fsync(fd) == 0 && fsync(d) == 0;
  if (fd >= 0) close(fd);
  close(d);
  if (!ok) return false;
  char *argv[] = {(char *)tool("HERMES_SECRET_TOOL", "secret-tool"), "clear", "application", HERMES_SLUG, "uri", (char *)uri, NULL};
  return secret_cmd(argv, NULL, NULL, 0) >= 0;
}
