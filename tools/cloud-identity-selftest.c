// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/cloud-identity-selftest.c -o /tmp/opencode/hermes-cloud-identity-test -pthread -lm
#include "../src/base.h"
#include <ctype.h>
#include "../src/cloud_identity.c"
#include <assert.h>
int main(void) {
  char name[160];
  cloud_unit_name("work s3", name, sizeof name);
  assert(!strcmp(name, "hermes-cloud-work_20s3.service"));
  cloud_legacy_unit_name("work s3", name, sizeof name);
  assert(!strcmp(name, "file-explorer-cloud-work_20s3.service"));
  char path[] = "/tmp/opencode/hermes-cloud-unit-XXXXXX";
  int fd = mkstemp(path); assert(fd >= 0);
  const char *text = "# Written by file-explorer for the cloud remote \"drive\". Edit it in the app: this file is replaced.\n";
  assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
  close(fd);
  assert(cloud_unit_owner(path) == CLOUD_LEGACY);
  fd = open(path, O_WRONLY | O_TRUNC); assert(fd >= 0);
  text = "# Written by hermes for the cloud remote \"drive\". Edit it in the app: this file is replaced.\n";
  assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
  close(fd);
  assert(cloud_unit_owner(path) == CLOUD_HERMES);
  fd = open(path, O_WRONLY | O_TRUNC); assert(fd >= 0);
  text = "[Service]\nExecStart=rclone mount drive: /external\n";
  assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text)); close(fd);
  assert(cloud_unit_owner(path) == CLOUD_EXTERNAL);
  unlink(path);
  puts("cloud identity fixtures passed");
  return 0;
}
