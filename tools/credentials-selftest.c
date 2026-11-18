// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/credentials-selftest.c -o /tmp/opencode/hermes-credentials-test -pthread -lm
#include "../src/base.h"
extern char **environ;
static const char *tool(const char *key, const char *fallback) {
  const char *v = getenv(key);
  return v ? v : fallback;
}
#include "../src/credentials.c"
#include <assert.h>

static void fake_secret_tool(const char *dir) {
  char path[PATH_MAX];
  snprintf(path, sizeof path, "%s/secret-tool", dir);
  FILE *f = fopen(path, "w");
  assert(f);
  fputs("#!/bin/sh\n"
        "op=$1; shift\n"
        "if [ \"$op\" = store ]; then shift 2; fi\n"
        "app=$2\n"
        "file=\"$CREDENTIAL_FIXTURE/$app.secret\"\n"
        "case \"$op\" in\n"
        "lookup) test -f \"$file\" && cat \"$file\";;\n"
        "store) test -z \"$FAIL_STORE\" || exit 1; cat > \"$file\";;\n"
        "clear) test -z \"$FAIL_CLEAR\" || exit 1; rm -f \"$file\";;\n"
        "esac\n", f);
  fclose(f);
  chmod(path, 0700);
  char toolpath[PATH_MAX];
  snprintf(toolpath, sizeof toolpath, "%s/secret-tool", dir);
  setenv("HERMES_SECRET_TOOL", toolpath, 1);
  setenv("CREDENTIAL_FIXTURE", dir, 1);
  setenv("XDG_STATE_HOME", dir, 1);
}

int main(void) {
  // Legacy-only data is ignored: no import and no fallback lookup.
  char legacy_root[] = "/tmp/opencode/hermes-credentials-legacy-XXXXXX";
  assert(mkdtemp(legacy_root));
  fake_secret_tool(legacy_root);
  char path[PATH_MAX];
  snprintf(path, sizeof path, "%s/file-explorer.secret", legacy_root);
  FILE *f = fopen(path, "w");
  assert(f);
  fputs("private-test-secret", f);
  fclose(f);
  char out[128];
  assert(!secret_lookup("sftp://fixture", out, sizeof out));

  char root[] = "/tmp/opencode/hermes-credentials-XXXXXX";
  assert(mkdtemp(root));
  fake_secret_tool(root);
  snprintf(path, sizeof path, "%s/hermes.secret", root);
  f = fopen(path, "w");
  assert(f);
  fputs("private-test-secret", f);
  fclose(f);
  assert(secret_lookup("sftp://fixture", out, sizeof out));
  assert(!strcmp(out, "private-test-secret"));

  setenv("FAIL_CLEAR", "1", 1);
  assert(!secret_clear("sftp://fixture"));
  assert(!secret_lookup("sftp://fixture", out, sizeof out));
  unsetenv("FAIL_CLEAR");
  assert(secret_clear("sftp://fixture"));
  setenv("FAIL_STORE", "1", 1);
  assert(!secret_store("sftp://fixture", "new"));
  assert(!secret_lookup("sftp://fixture", out, sizeof out));
  unsetenv("FAIL_STORE");
  assert(secret_store("sftp://fixture", "new"));
  assert(secret_lookup("sftp://fixture", out, sizeof out) && !strcmp(out, "new"));
  explicit_bzero(out, sizeof out);
  puts("credential fixtures passed");
  return 0;
}
