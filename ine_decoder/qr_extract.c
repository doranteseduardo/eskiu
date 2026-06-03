/*
 * qr_extract.c — thin C shim bridging Eskiu's extern to ine-qr-c's qr_extract()
 *
 * ine-qr-c exposes:  QRPair qr_extract(const char *path)  (returns by value)
 * Eskiu needs:       int ine_qr_extract(string path, *QRPair out)
 *
 * This file just wraps the value-return into a pointer-out convention that
 * Eskiu's C ABI can call cleanly.
 */

#include <stdint.h>
#include <string.h>

/* Forward declaration matching ine-qr-c/include/qr_extract.h */
typedef struct {
    uint8_t left[858];
    uint8_t right[858];
    int     ok;
    char    err[256];
} QRPair;

#ifdef __cplusplus
extern "C" {
#endif

QRPair qr_extract(const char *path);  /* defined in ine-qr-c/src/qr_extract.o */

/* Called by Eskiu: fills *out, returns 1 on success / 0 on failure */
int ine_qr_extract(const char* path, QRPair* out) {
    if (!path || !out) return 0;
    *out = qr_extract(path);
    return out->ok;
}

#ifdef __cplusplus
}
#endif
