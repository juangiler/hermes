// preview_meta.h - shared metadata for preview decoding (base.h included first).
#pragma once

typedef struct {
    int w, h;
    float dpi_x, dpi_y;
} ImageMeta;

typedef struct {
    int duration_ms, w, h;
    char codec[32];
} VideoMeta;

typedef struct {
    int pages;
    char title[128], author[128];
} DocMeta;

bool preview_alpha_capable(const char *ext);
bool image_dpi_info(const char *path, float *dpi_x, float *dpi_y);
u8 *preview_downscale(u8 *src, int w, int h, int max_px, int *ow, int *oh);
bool parse_ffprobe_output(const char *text, VideoMeta *out);
bool parse_pdfinfo_output(const char *text, DocMeta *out);
bool probe_video(const char *path, VideoMeta *out);
bool probe_pdf(const char *path, DocMeta *out);
void pdf_page_cache_path(const char *cache_dir, const char *path, char *out, size_t cap);
bool render_pdf_page(const char *path, const char *cache_dir, int max_px, char *out_png, size_t cap);
