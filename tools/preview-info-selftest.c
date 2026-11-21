/* cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-format-truncation
   tools/preview-info-selftest.c -o /tmp/opencode/hermes-preview-info-test -lm -ldl -pthread */
#include "../src/base.h"
#include <assert.h>
#include "../src/preview_meta.h"

// stb is included here (not in image_meta.c) so later tasks can add ico_preview.c
// to this tool with the same include order and one shared implementation.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

#include "../src/image_meta.c"

static void write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static u32 crc32(const u8 *data, size_t len) {
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    return c ^ 0xFFFFFFFFu;
}

static void be32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

// PNG chunk: 4-byte big-endian length, 4-byte type, payload, 4-byte CRC over type+payload.
static size_t png_chunk(u8 *out, const char type[4], const u8 *data, u32 len) {
    be32(out, len);
    memcpy(out + 4, type, 4);
    if (len) memcpy(out + 8, data, len);
    be32(out + 8 + len, crc32(out + 4, 4 + len));
    return 12u + len;
}

static void write_png_with_phys(const char *path, u32 x, u32 y, u8 unit) {
    static const u8 sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    u8 buf[128];
    size_t n = 0;
    memcpy(buf + n, sig, 8); n += 8;
    const u8 ihdr[13] = {0, 0, 0, 1, 0, 0, 0, 1, 8, 2, 0, 0, 0}; // 1x1, 8-bit RGB
    n += png_chunk(buf + n, "IHDR", ihdr, 13);
    u8 phys[9];
    be32(phys, x); be32(phys + 4, y); phys[8] = unit;
    n += png_chunk(buf + n, "pHYs", phys, 9);
    n += png_chunk(buf + n, "IEND", NULL, 0);
    write_file(path, buf, n);
}

// Minimal JPEG: SOI, APP1(Exif\0\0 + little-endian TIFF with X/YResolution), EOI.
static void write_jpeg_with_exif_dpi(const char *path, u32 dpi) {
    u8 tiff[80];
    memset(tiff, 0, sizeof tiff);
    tiff[0] = 'I'; tiff[1] = 'I';          // byte order
    tiff[2] = 0x2A; tiff[3] = 0x00;        // magic 42
    tiff[4] = 0x08;                        // IFD0 offset = 8
    tiff[8] = 0x03; tiff[9] = 0x00;        // 3 IFD0 entries
    tiff[10] = 0x1A; tiff[11] = 0x01;      // tag 0x011A XResolution
    tiff[12] = 0x05; tiff[13] = 0x00;      // type RATIONAL
    tiff[14] = 0x01;                       // count 1
    tiff[18] = 50;                         // value offset (LE)
    tiff[22] = 0x1B; tiff[23] = 0x01;      // tag 0x011B YResolution
    tiff[24] = 0x05; tiff[25] = 0x00;
    tiff[26] = 0x01;
    tiff[30] = 58;                         // value offset (LE)
    tiff[34] = 0x28; tiff[35] = 0x01;      // tag 0x0128 ResolutionUnit
    tiff[36] = 0x03; tiff[37] = 0x00;      // type SHORT
    tiff[38] = 0x01;                       // count 1
    tiff[42] = 0x02;                       // value 2 = inch
    tiff[50] = (u8)dpi; tiff[51] = (u8)(dpi >> 8); tiff[52] = (u8)(dpi >> 16); tiff[53] = (u8)(dpi >> 24);
    tiff[54] = 1;                          // XResolution denominator
    tiff[58] = (u8)dpi; tiff[59] = (u8)(dpi >> 8); tiff[60] = (u8)(dpi >> 16); tiff[61] = (u8)(dpi >> 24);
    tiff[62] = 1;                          // YResolution denominator

    u8 jpeg[128];
    size_t n = 0;
    jpeg[n++] = 0xFF; jpeg[n++] = 0xD8;    // SOI
    jpeg[n++] = 0xFF; jpeg[n++] = 0xE1;    // APP1
    u16 seglen = 2 + 6 + 66;
    jpeg[n++] = (u8)(seglen >> 8); jpeg[n++] = (u8)seglen;
    memcpy(jpeg + n, "Exif\0\0", 6); n += 6;
    memcpy(jpeg + n, tiff, 66); n += 66;
    jpeg[n++] = 0xFF; jpeg[n++] = 0xD9;    // EOI
    write_file(path, jpeg, n);
}

static void test_alpha_capable(void) {
    const char *yes[] = {"png", "ico", "svg", "webp", "gif"};
    const char *no[] = {"jpg", "bmp", "mp4", "pdf", "txt"};
    for (size_t i = 0; i < ARRAY_COUNT(yes); i++) assert(preview_alpha_capable(yes[i]));
    for (size_t i = 0; i < ARRAY_COUNT(no); i++) assert(!preview_alpha_capable(no[i]));
}

static void test_image_dpi(void) {
    char path[] = "/tmp/opencode/hermes-dpi-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);

    write_png_with_phys(path, 11811, 11811, 1); // pixels per metre
    float x = 0, y = 0;
    assert(image_dpi_info(path, &x, &y));
    assert(fabsf(x - 300.f) < 0.2f && fabsf(y - 300.f) < 0.2f);

    write_png_with_phys(path, 9449, 9449, 0); // unknown unit
    x = y = 0;
    assert(!image_dpi_info(path, &x, &y));

    write_jpeg_with_exif_dpi(path, 240);
    x = y = 0;
    assert(image_dpi_info(path, &x, &y));
    assert(fabsf(x - 240.f) < 0.5f && fabsf(y - 240.f) < 0.5f);

    write_file(path, "not an image", 12);
    x = y = 0;
    assert(!image_dpi_info(path, &x, &y));
    unlink(path);
}

int main(void) {
    test_alpha_capable();
    test_image_dpi();
    puts("Preview info: passed");
    return 0;
}
