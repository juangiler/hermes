// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/integration-identity-selftest.c -o /tmp/opencode/hermes-integration-identity-test -pthread -lm
#include "../src/base.h"
#include "../src/integration_identity.c"
#include <assert.h>
int main(void) {
  const char *original = "[Desktop Entry]\nName=File Explorer\nExec=/usr/bin/file-explorer %F\nIcon=dev.fileexplorer\nStartupWMClass=dev.fileexplorer\n[Desktop Action Mine]\nExec=my-script\nIcon=my-icon\n";
  char *adopted = identity_legacy_desktop(original, "/fixture/hermes");
  assert(adopted && strstr(adopted, "Name=Hermes\n") && strstr(adopted, "Exec=\"/fixture/hermes\" %F\n"));
  assert(strstr(adopted, "[Desktop Action Mine]\nExec=my-script\nIcon=my-icon\n"));
  free(adopted);
  assert(!identity_legacy_desktop("[Desktop Entry]\nExec=unrelated\n", "/fixture/hermes"));
  puts("integration ownership fixtures passed");
  return 0;
}
