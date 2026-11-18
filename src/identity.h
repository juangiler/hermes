#pragma once
#define HERMES_NAME "Hermes"
#define HERMES_SLUG "hermes"
#define HERMES_APP_ID "io.github.juangiler.hermes"
#define HERMES_LEGACY_SLUG "file-explorer"
#define HERMES_LEGACY_APP_ID "dev.fileexplorer"

// Presence, including an empty value, takes precedence over the legacy alias.
static const char *identity_env(const char *name) {
  if (!strncmp(name, "FE_", 3)) {
    char key[256];
    snprintf(key, sizeof key, "HERMES_%s", name + 3);
    const char *value = getenv(key);
    if (value) return value;
  }
  return getenv(name);
}

static void identity_xdg(const char *variable, const char *fallback, const char *slug,
                         char *out, size_t cap) {
  const char *x = getenv(variable), *home = getenv("HOME");
  if (x && x[0] == '/') snprintf(out, cap, "%s/%s", x, slug);
  else snprintf(out, cap, "%s/%s/%s", home && home[0] == '/' ? home : "/tmp", fallback, slug);
}
