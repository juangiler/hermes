// image_meta.c - shared metadata helpers used by preview decoding.
#include "preview_meta.h"

bool preview_alpha_capable(const char *ext) {
    static const char *const alpha[] = {"png", "ico", "svg", "webp", "gif"};
    for (size_t i = 0; i < ARRAY_COUNT(alpha); i++)
        if (!strcmp(ext, alpha[i])) return true;
    return false;
}

// --- byte readers (metadata uses both PNG big-endian and TIFF little/big) ---

static u16 img_be16(const u8 *p) { return (u16)(((u16)p[0] << 8) | (u16)p[1]); }
static u32 img_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
static u16 img_le16(const u8 *p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }
static u32 img_le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// --- PNG pHYs (pixels per metre, unit byte 1) -------------------------------

static bool img_png_dpi(const u8 *buf, size_t len, float *dx, float *dy) {
    static const u8 sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (len < 8 || memcmp(buf, sig, 8)) return false;
    size_t off = 8;
    while (off + 12 <= len) {
        u32 clen = img_be32(buf + off);
        if (clen > len - off - 12) return false; // truncated or bogus chunk length
        const u8 *type = buf + off + 4;
        const u8 *data = buf + off + 8;
        if (clen == 9 && !memcmp(type, "pHYs", 4) && data[8] == 1) {
            u32 px = img_be32(data), py = img_be32(data + 4);
            if (px && py) {
                *dx = (float)px * 0.0254f; // pixels/metre -> pixels/inch
                *dy = (float)py * 0.0254f;
                return true;
            }
        }
        off += 12 + (size_t)clen;
        if (!memcmp(type, "IEND", 4)) break;
    }
    return false;
}

// --- JPEG APP1/Exif TIFF resolution tags -----------------------------------

static bool img_tiff_dpi(const u8 *t, size_t len, float *dx, float *dy) {
    if (len < 8) return false;
    bool le;
    if (t[0] == 'I' && t[1] == 'I') le = true;
    else if (t[0] == 'M' && t[1] == 'M') le = false;
    else return false;
    u16 (*r16)(const u8 *) = le ? img_le16 : img_be16;
    u32 (*r32)(const u8 *) = le ? img_le32 : img_be32;
    if (r16(t + 2) != 42) return false; // TIFF magic

    u32 ifd = r32(t + 4);
    if (ifd > len || len - ifd < 6) return false; // count + at least one entry + next-IFD
    u16 n = r16(t + ifd);
    if (6 + (size_t)n * 12 > len - ifd) return false;

    float xr = 0, yr = 0;
    u32 unit = 2; // inch when ResolutionUnit is absent
    bool have_x = false, have_y = false;
    const u8 *e = t + ifd + 2;
    for (u16 i = 0; i < n; i++, e += 12) {
        u16 tag = r16(e), type = r16(e + 2);
        u32 count = r32(e + 4);
        if (tag == 0x011A || tag == 0x011B) { // XResolution / YResolution: RATIONAL
            if (type != 5 || count < 1) continue;
            u32 off = r32(e + 8);
            if (off > len || len - off < 8) continue;
            u32 num = r32(t + off), den = r32(t + off + 4);
            if (!den) continue;
            float v = (float)num / (float)den;
            if (tag == 0x011A) { xr = v; have_x = true; }
            else { yr = v; have_y = true; }
        } else if (tag == 0x0128) { // ResolutionUnit: SHORT (2 = inch, 3 = cm)
            if (type != 3 || count < 1) continue;
            unit = r16(e + 8);
        }
    }
    if (!have_x || !have_y) return false;
    float scale = unit == 2 ? 1.f : unit == 3 ? 2.54f : 0.f;
    if (!scale) return false;
    *dx = xr * scale;
    *dy = yr * scale;
    return true;
}

static bool img_jpeg_dpi(const u8 *buf, size_t len, float *dx, float *dy) {
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return false; // SOI
    size_t off = 2;
    while (off + 1 < len) {
        if (buf[off] != 0xFF) { off++; continue; } // tolerate stray bytes before a marker
        while (off < len && buf[off] == 0xFF) off++; // fill bytes
        if (off >= len) break;
        u8 marker = buf[off++];
        if (marker == 0x00 || marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue; // stuffed byte / SOI / TEM / RST: no length field
        if (marker == 0xD9 || marker == 0xDA) break; // EOI / SOS: no metadata follows
        if (off + 2 > len) break;
        u32 slen = ((u32)buf[off] << 8) | buf[off + 1];
        if (slen < 2 || slen > len - off) break;
        const u8 *seg = buf + off + 2;
        size_t payload = slen - 2;
        if (marker == 0xE1 && payload >= 6 && !memcmp(seg, "Exif\0\0", 6))
            return img_tiff_dpi(seg + 6, payload - 6, dx, dy);
        off += slen;
    }
    return false;
}

bool image_dpi_info(const char *path, float *dpi_x, float *dpi_y) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode)) { fclose(f); return false; }
    size_t cap = 1u << 20; // 1 MiB: PNG chunk scan; JPEG EXIF is bounded to 256 KiB below
    if ((u64)st.st_size < cap) cap = (size_t)st.st_size;
    if (!cap) { fclose(f); return false; }
    u8 *buf = malloc(cap);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, cap, f);
    fclose(f);

    float x = 0, y = 0;
    bool ok = img_png_dpi(buf, got, &x, &y); // checks the PNG signature itself
    if (!ok) ok = img_jpeg_dpi(buf, MIN(got, (size_t)256u << 10), &x, &y);
    free(buf);
    if (ok) { *dpi_x = x; *dpi_y = y; }
    return ok; // on failure, outputs are untouched
}

// Box-filter downscale RGBA so the longest side is <= max_px. Owns `src` once a
// smaller buffer is produced (stb uses the default malloc/free allocator).
u8 *preview_downscale(u8 *src, int w, int h, int max_px, int *ow, int *oh) {
    int f = 1;
    while ((w / f) > max_px || (h / f) > max_px) f++;
    if (f == 1) { *ow = w; *oh = h; return src; }
    int nw = w / f, nh = h / f;
    u8 *dst = malloc((size_t)nw * nh * 4);
    if (!dst) { *ow = w; *oh = h; return src; }
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            u32 acc[4] = {0, 0, 0, 0};
            for (int yy = 0; yy < f; yy++) {
                const u8 *row = src + ((size_t)(y * f + yy) * w + (size_t)x * f) * 4;
                for (int xx = 0; xx < f; xx++)
                    for (int c = 0; c < 4; c++) acc[c] += row[xx * 4 + c];
            }
            u8 *o = dst + ((size_t)y * nw + x) * 4;
            for (int c = 0; c < 4; c++) o[c] = (u8)(acc[c] / (u32)(f * f));
        }
    free(src);
    *ow = nw;
    *oh = nh;
    return dst;
}
