// ico_preview.c - direct ICO decoding for previews (unity build: include after preview.c).
#include "preview_meta.h"

#define ICO_MAX_FILE ((u64)32 << 20)

static u16 ico_le16(const u8 *p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }
static u32 ico_le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

u8 *ico_preview_pixels(const char *path, int max_px, int *w, int *h, int *src_w, int *src_h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size < 22 ||
        (u64)st.st_size > ICO_MAX_FILE) { fclose(f); return NULL; }
    size_t len = (size_t)st.st_size;
    u8 *buf = malloc(len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, len, f) != len) { free(buf); fclose(f); return NULL; }
    fclose(f);

    if (ico_le16(buf) || ico_le16(buf + 2) != 1) { free(buf); return NULL; }
    int count = ico_le16(buf + 4);
    if (count < 1 || 22 + (size_t)count * 16 > len) { free(buf); return NULL; }

    int best = 0;
    u64 best_area = 0;
    for (int i = 0; i < count; i++) {
        const u8 *e = buf + 6 + (size_t)i * 16;
        u64 area = (u64)(e[0] ? e[0] : 256) * (u64)(e[1] ? e[1] : 256);
        if (area > best_area) { best_area = area; best = i; }
    }
    const u8 *e = buf + 6 + (size_t)best * 16;
    int ew = e[0] ? e[0] : 256, eh = e[1] ? e[1] : 256;
    u32 dlen = ico_le32(e + 8), doff = ico_le32(e + 12);
    if (!dlen || doff < 22 || (u64)doff + dlen > (u64)len) { free(buf); return NULL; }
    const u8 *p = buf + doff;

    static const u8 png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    int pw = 0, ph = 0;
    u8 *pixels;
    if (dlen >= 8 && !memcmp(p, png_sig, 8)) {
        pixels = stbi_load_from_memory(p, (int)dlen, &pw, &ph, NULL, 4);
        if (!pixels) { free(buf); return NULL; }
    } else {
        if (dlen < 40 || ico_le32(p) < 40 ||
            ico_le16(p + 12) != 1 || ico_le16(p + 14) != 32) { free(buf); return NULL; }
        pw = (int)ico_le32(p + 4);
        int bh = (int)ico_le32(p + 8);
        if (pw <= 0 || bh <= 0) { free(buf); return NULL; }
        ph = bh / 2 + bh % 2; // ICO DIB height counts the XOR and AND masks
        size_t hdr = ico_le32(p);
        size_t stride = ((size_t)pw * 4 + 3) & ~(size_t)3;
        if (hdr > dlen || (u64)hdr + (u64)stride * (u64)ph > (u64)dlen) {
            free(buf);
            return NULL;
        }
        pixels = malloc((size_t)pw * (size_t)ph * 4);
        if (!pixels) { free(buf); return NULL; }
        for (int y = 0; y < ph; y++) {
            const u8 *src = p + hdr + (size_t)(ph - 1 - y) * stride; // DIB rows are bottom-up
            u8 *dst = pixels + (size_t)y * (size_t)pw * 4;
            for (int x = 0; x < pw; x++, src += 4, dst += 4) {
                dst[0] = src[2]; // R
                dst[1] = src[1]; // G
                dst[2] = src[0]; // B
                dst[3] = src[3]; // alpha from the XOR bitmap; AND mask ignored
            }
        }
    }
    free(buf);

    u8 *out = preview_downscale(pixels, pw, ph, max_px, w, h);
    *src_w = ew;
    *src_h = eh;
    return out;
}
