// media_probe.c - optional, bounded probes for video and PDF metadata.
// Included by main.c (unity build) and by tools/preview-info-selftest.c.
// ffprobe/pdfinfo/pdftoppm are runtime-only tools: no tool, no output.
#include <poll.h>

#define PROBE_OUT_CAP (64u * 1024u)
#define PROBE_TIMEOUT_MS 8000

// Return a trimmed view of the value on the first line starting with key.
// The key includes its separator (e.g. "duration=" or "Pages:").
typedef struct {
    const char *s, *e;
} ProbeSpan;

static ProbeSpan probe_field(const char *text, const char *key) {
    ProbeSpan none = {NULL, NULL};
    if (!text || !key) return none;
    size_t kl = strlen(key);
    while (*text) {
        const char *line = text;
        while (*text && *text != '\n' && *text != '\r') text++;
        const char *end = text;
        while (*text == '\n' || *text == '\r') text++;
        if ((size_t)(end - line) >= kl && !memcmp(line, key, kl)) {
            const char *s = line + kl, *e = end;
            while (s < e && (*s == ' ' || *s == '\t')) s++;
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
            ProbeSpan r = {s, e};
            return r;
        }
    }
    return none;
}

static void probe_span_copy(char *dst, size_t cap, ProbeSpan v) {
    if (!cap || !v.s) return;
    size_t n = (size_t)(v.e - v.s);
    if (n >= cap) n = cap - 1;
    memcpy(dst, v.s, n);
    dst[n] = 0;
}

// Run a helper with its stdout captured into out (up to cap bytes), its stdin and
// stderr on /dev/null. Reading is nonblocking and stops at the deadline; a child
// still running then is killed with SIGKILL. Returns true only on stdout EOF.
static bool probe_run(char *const argv[], char *out, size_t cap, int timeout_ms) {
    if (!argv || !argv[0] || !out || cap < 2) return false;
    out[0] = 0;
    if (timeout_ms <= 0) timeout_ms = PROBE_TIMEOUT_MS;

    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return false;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (p[0] > 2) posix_spawn_file_actions_addclose(&fa, p[0]);
    posix_spawn_file_actions_adddup2(&fa, p[1], STDOUT_FILENO);
    if (p[1] > 2) posix_spawn_file_actions_addclose(&fa, p[1]);
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, spawn_attr(), argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    if (rc != 0) { close(p[0]); return false; }

    int fl = fcntl(p[0], F_GETFL, 0);
    if (fl >= 0) fcntl(p[0], F_SETFL, fl | O_NONBLOCK);

    u64 deadline = now_ns() + (u64)timeout_ms * 1000000ull;
    size_t used = 0;
    char sink[512];
    bool eof = false;
    for (;;) {
        u64 left = now_ns() < deadline ? deadline - now_ns() : 0;
        if (!left) break;
        bool keep = used + 1 < cap;
        char *dst = keep ? out + used : sink;
        size_t room = keep ? cap - used - 1 : sizeof sink;
        ssize_t n = read(p[0], dst, room);
        if (n > 0) {
            if (keep) used += (size_t)n;
            continue;
        }
        if (n == 0) { eof = true; break; }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) break;
        struct pollfd w = {.fd = p[0], .events = POLLIN};
        int ms = (int)((left + 999999ull) / 1000000ull);
        int pr = poll(&w, 1, ms);
        if (pr > 0 || (pr < 0 && errno == EINTR)) continue;
        break;
    }
    if (!eof) kill(pid, SIGKILL);
    close(p[0]);
    // SIGCHLD is ignored app-wide (children reap themselves); this only matters
    // for standalone tools, where it keeps the child from lingering as a zombie.
    int st;
    for (int i = 0; i < 200; i++) {
        pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid || (w < 0 && errno != EINTR)) break;
        usleep(1000);
    }
    out[used] = 0;
    return eof;
}

bool parse_ffprobe_output(const char *text, VideoMeta *out) {
    if (!text || !out) return false;
    ProbeSpan codec = probe_field(text, "codec_name=");
    ProbeSpan w = probe_field(text, "width=");
    ProbeSpan h = probe_field(text, "height=");
    ProbeSpan dur = probe_field(text, "duration=");
    if (!codec.s || !w.s || !h.s || !dur.s) return false;
    char cbuf[64], wbuf[32], hbuf[32], dbuf[64];
    probe_span_copy(cbuf, sizeof cbuf, codec);
    probe_span_copy(wbuf, sizeof wbuf, w);
    probe_span_copy(hbuf, sizeof hbuf, h);
    probe_span_copy(dbuf, sizeof dbuf, dur);
    if (!cbuf[0]) return false;

    char *end = NULL;
    long iw = strtol(wbuf, &end, 10);
    if (end == wbuf || *end || iw <= 0 || iw > INT_MAX) return false;
    long ih = strtol(hbuf, &end, 10);
    if (end == hbuf || *end || ih <= 0 || ih > INT_MAX) return false;
    float secs = strtof(dbuf, &end);
    if (end == dbuf || *end || !isfinite(secs) || secs < 0.f) return false;
    long ms = lroundf(secs * 1000.f);
    if (ms < 0 || ms > INT_MAX) return false;

    static const struct {
        const char *from, *to;
    } names[] = {
        {"h264", "H.264"}, {"hevc", "HEVC"}, {"vp9", "VP9"},
        {"av1", "AV1"}, {"mpeg4", "MPEG-4"},
    };
    const char *name = cbuf;
    for (size_t i = 0; i < ARRAY_COUNT(names); i++) {
        if (str_ieq(cbuf, names[i].from)) { name = names[i].to; break; }
    }
    VideoMeta tmp = {0};
    tmp.duration_ms = (int)ms;
    tmp.w = (int)iw;
    tmp.h = (int)ih;
    size_t nl = strlen(name);
    if (nl >= sizeof tmp.codec) nl = sizeof tmp.codec - 1;
    memcpy(tmp.codec, name, nl);
    tmp.codec[nl] = 0;
    *out = tmp;
    return true;
}

bool parse_pdfinfo_output(const char *text, DocMeta *out) {
    if (!text || !out) return false;
    ProbeSpan title = probe_field(text, "Title:");
    ProbeSpan author = probe_field(text, "Author:");
    ProbeSpan pages = probe_field(text, "Pages:");
    if (!pages.s) return false;
    char pbuf[32];
    probe_span_copy(pbuf, sizeof pbuf, pages);
    char *end = NULL;
    long n = strtol(pbuf, &end, 10);
    if (end == pbuf || *end || n <= 0 || n > INT_MAX) return false;
    DocMeta tmp = {0};
    probe_span_copy(tmp.title, sizeof tmp.title, title);
    probe_span_copy(tmp.author, sizeof tmp.author, author);
    tmp.pages = (int)n;
    *out = tmp;
    return true;
}

bool probe_video(const char *path, VideoMeta *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!path || !path[0]) return false;
    char buf[PROBE_OUT_CAP];
    char *av[] = {"ffprobe", "-v", "error", "-select_streams", "v:0",
                  "-show_entries", "stream=codec_name,width,height:format=duration",
                  "-of", "default=noprint_wrappers=1:nokey=0", (char *)path, NULL};
    if (!probe_run(av, buf, sizeof buf, PROBE_TIMEOUT_MS)) return false;
    return parse_ffprobe_output(buf, out);
}

bool probe_pdf(const char *path, DocMeta *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!path || !path[0]) return false;
    char buf[PROBE_OUT_CAP];
    char *av[] = {"pdfinfo", (char *)path, NULL};
    if (!probe_run(av, buf, sizeof buf, PROBE_TIMEOUT_MS)) return false;
    return parse_pdfinfo_output(buf, out);
}

static u32 probe_fnv1a(const char *s) {
    u32 hash = 2166136261u;
    for (; *s; s++) {
        hash ^= (u8)*s;
        hash *= 16777619u;
    }
    return hash;
}

void pdf_page_cache_path(const char *cache_dir, const char *path, char *out, size_t cap) {
    if (!out || !cap) return;
    if (!cache_dir || !cache_dir[0]) cache_dir = "/tmp";
    snprintf(out, cap, "%s/pdf-%08x.png", cache_dir, probe_fnv1a(path ? path : ""));
}

bool render_pdf_page(const char *path, const char *cache_dir, int max_px, char *out_png, size_t cap) {
    if (!path || !path[0]) return false;
    char png[PATH_MAX], base[PATH_MAX];
    pdf_page_cache_path(cache_dir, path, png, sizeof png);
    snprintf(base, sizeof base, "%s", png);
    size_t bl = strlen(base);
    if (bl > 4 && !strcmp(base + bl - 4, ".png")) base[bl - 4] = 0;
    else return false;

    char sz[16];
    snprintf(sz, sizeof sz, "%d", CLAMP(max_px, 256, 2048));
    char *av[] = {"pdftoppm", "-f", "1", "-l", "1", "-singlefile", "-png",
                  "-scale-to", sz, (char *)path, base, NULL};
    unlink(png); // a stale cache file must not be reported when the tool is missing
    char scratch[64];
    if (!probe_run(av, scratch, sizeof scratch, PROBE_TIMEOUT_MS)) return false;
    struct stat st;
    if (stat(png, &st) != 0 || st.st_size <= 0) return false;
    if (out_png && cap) snprintf(out_png, cap, "%s", png);
    return true;
}
