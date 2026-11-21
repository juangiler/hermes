// preview_meta.h - shared metadata for preview decoding (base.h included first).
#pragma once

typedef struct {
    int w, h;
    float dpi_x, dpi_y;
} ImageMeta;

bool preview_alpha_capable(const char *ext);
bool image_dpi_info(const char *path, float *dpi_x, float *dpi_y);
u8 *preview_downscale(u8 *src, int w, int h, int max_px, int *ow, int *oh);
