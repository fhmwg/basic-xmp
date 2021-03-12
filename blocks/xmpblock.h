#include <stddef.h> // for size_t

/**
 * The data returned by the xmp_from_... functions.
 * `packets` is a malloced array of malloced strings.
 * `width` and `packet` will both be 0 if the file was in the wrong format.
 */
typedef struct {
    unsigned width;
    unsigned height;
    size_t num_packets;
    char **packets;
} xmp_rdata;

xmp_rdata xmp_from_gif(const char *filename);
xmp_rdata xmp_from_isobmf(const char *filename);
xmp_rdata xmp_from_jpeg(const char *filename);
xmp_rdata xmp_from_png(const char *filename);
xmp_rdata xmp_from_webp(const char *filename);
xmp_rdata xmp_from_tiff(const char *filename);

/// returns true on success, false on failure.
/// Will fail if `dest` already exists.
int xmp_to_gif(const char *ref, const char *dest, const char *xmp);
int xmp_to_isobmf(const char *ref, const char *dest, const char *xmp);
int xmp_to_jpeg(const char *ref, const char *dest, const char *xmp);
int xmp_to_png(const char *ref, const char *dest, const char *xmp);
int xmp_to_webp(const char *ref, const char *dest, const char *xmp);

