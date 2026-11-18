#pragma once
#define HERMES_NAME "Hermes"
#define HERMES_SLUG "hermes"
#define HERMES_APP_ID "io.github.juangiler.hermes"

static const char *identity_env(const char *name) {
  return getenv(name);
}

static void identity_xdg(const char *variable, const char *fallback, const char *slug,
                         char *out, size_t cap) {
  const char *x = getenv(variable), *home = getenv("HOME");
  if (x && x[0] == '/') snprintf(out, cap, "%s/%s", x, slug);
  else snprintf(out, cap, "%s/%s/%s", home && home[0] == '/' ? home : "/tmp", fallback, slug);
}
