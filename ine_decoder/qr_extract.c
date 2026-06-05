/*
 * qr_extract.c — C shim: Eskiu extern → CoreGraphics + zxing-cpp implementation
 *
 * Eskiu declares:  extern int ine_qr_extract(string path, *QRPair out)
 * This shim calls: ine_qr_extract_impl() from qr_extract_impl.cpp
 *
 * Using our own CoreGraphics implementation instead of ine-qr-c's qr_extract()
 * because CoreGraphics decodes HEIC natively in-process (~70 ms) vs ine-qr-c's
 * sips shell-out approach (~185 ms).
 */

#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t left[858];
    uint8_t right[858];
    int     ok;
    char    err[256];
} QRPair;

#ifdef __cplusplus
extern "C" {
#endif

/* Defined in qr_extract_impl.cpp (CoreGraphics + zxing-cpp) */
int ine_qr_extract_impl(const char* image_path, QRPair* out);

int ine_qr_extract(const char* path, QRPair* out) {
    if (!path || !out) return 0;
    memset(out, 0, sizeof(*out));
    return ine_qr_extract_impl(path, out);
}

#ifdef __cplusplus
}
#endif
