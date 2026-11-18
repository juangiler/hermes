enum { CLOUD_EXTERNAL, CLOUD_HERMES };
static void cloud_identity_unit_name(const char *prefix, const char *name, char *out, size_t cap) {
  size_t o = (size_t)snprintf(out, cap, "%s-cloud-", prefix);
  for (const char *c = name; *c && o + 12 < cap; c++)
    o += isalnum((u8)*c) || *c == '-' || *c == '.' ? (size_t)snprintf(out + o, cap - o, "%c", *c) : (size_t)snprintf(out + o, cap - o, "_%02x", (u8)*c);
  if (o < cap) snprintf(out + o, cap - o, ".service");
}
static void cloud_unit_name(const char *name, char *out, size_t cap) {
  cloud_identity_unit_name(HERMES_SLUG, name, out, cap);
}
static int cloud_unit_owner(const char *path) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return CLOUD_EXTERNAL;
  char head[256] = "";
  ssize_t n = read(fd, head, sizeof head - 1);
  close(fd);
  if (n <= 0) return CLOUD_EXTERNAL;
  const char *hermes = "# Written by hermes for the cloud remote \"";
  return !strncmp(head, hermes, strlen(hermes)) ? CLOUD_HERMES : CLOUD_EXTERNAL;
}
