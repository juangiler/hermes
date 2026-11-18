// Strict provenance checks: a standard filename alone never proves ownership.
static char *identity_legacy_desktop(const char *text, const char *exe) {
  if (!strstr(text, "\nIcon=" HERMES_LEGACY_APP_ID "\n") ||
      !strstr(text, "\nStartupWMClass=" HERMES_LEGACY_APP_ID "\n") ||
      strpbrk(exe, "\"\\\n\r`$")) return NULL;
  char *out = NULL;
  size_t size = 0;
  FILE *f = open_memstream(&out, &size);
  if (!f) return NULL;
  bool main = false, changed = false;
  for (const char *line = text; *line;) {
    size_t n = strcspn(line, "\n");
    if (line[0] == '[') main = n == 15 && !strncmp(line, "[Desktop Entry]", n);
    if (main && n == 18 && !strncmp(line, "Name=File Explorer", n)) fputs("Name=Hermes\n", f);
    else if (main && n == strlen("Icon=" HERMES_LEGACY_APP_ID) && !strncmp(line, "Icon=" HERMES_LEGACY_APP_ID, n))
      fputs("Icon=" HERMES_APP_ID "\n", f);
    else if (main && n == strlen("StartupWMClass=" HERMES_LEGACY_APP_ID) && !strncmp(line, "StartupWMClass=" HERMES_LEGACY_APP_ID, n))
      fputs("StartupWMClass=" HERMES_APP_ID "\n", f);
    else if (main && !strncmp(line, "Exec=", 5)) {
      const char *start = line + 5, *end = start;
      bool quoted = *start == '"';
      if (quoted) start++;
      end = start;
      while (end < line + n && (quoted ? *end != '"' : *end != ' ')) end++;
      const char *base = end;
      while (base > start && base[-1] != '/') base--;
      if ((size_t)(end - base) == strlen(HERMES_LEGACY_SLUG) && !memcmp(base, HERMES_LEGACY_SLUG, (size_t)(end - base))) {
        const char *args = end + (quoted && end < line + n ? 1 : 0);
        fprintf(f, "Exec=\"%s\"%.*s\n", exe, (int)(line + n - args), args);
        changed = true;
      } else fprintf(f, "%.*s\n", (int)n, line);
    } else fprintf(f, "%.*s\n", (int)n, line);
    line += n + (line[n] == '\n');
  }
  fclose(f);
  if (!changed) { free(out); return NULL; }
  return out;
}
