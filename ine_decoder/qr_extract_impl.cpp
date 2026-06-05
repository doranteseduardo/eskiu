/*
 * qr_extract_impl.cpp — INE QR extraction: CoreGraphics + zxing-cpp
 *
 * Loads any image format macOS supports (HEIC, JPEG, PNG, …), converts
 * to 8-bit grayscale, and uses zxing-cpp to detect the two QR codes on
 * the credential.  The results are sorted by X position so left/right
 * assignment is stable regardless of detection order.
 *
 * Build (macOS):
 *   c++ -std=c++17 -O2 -c qr_extract_impl.cpp \
 *       -I$(brew --prefix zxing-cpp)/include \
 *       -o qr_extract_impl.o
 *
 * Link (add to final clang command):
 *   -framework CoreFoundation -framework CoreGraphics -framework ImageIO
 *   -L$(brew --prefix zxing-cpp)/lib -lZXing
 *   -lstdc++
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>

// macOS image loading
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

// zxing-cpp barcode detection (v2.x API)
#include <ZXing/ReadBarcode.h>
#include <ZXing/BarcodeFormat.h>

extern "C" {

// Must match the Eskiu struct QRPair layout exactly
typedef struct {
    uint8_t left[858];
    uint8_t right[858];
    int     ok;
    char    err[256];
} QRPair;

// ── Image loading ─────────────────────────────────────────────────────────

/*
 * Load any CoreGraphics-supported image as a flat 8-bit grayscale buffer.
 * Returns heap-allocated pixel data (caller must free()) or nullptr on error.
 * Sets *out_w and *out_h on success.
 */
static uint8_t* load_image_gray(const char* path, int* out_w, int* out_h) {
    // Build a file URL from the path string
    CFStringRef cfpath = CFStringCreateWithCString(
        kCFAllocatorDefault, path, kCFStringEncodingUTF8);
    if (!cfpath) return nullptr;

    CFURLRef url = CFURLCreateWithFileSystemPath(
        kCFAllocatorDefault, cfpath, kCFURLPOSIXPathStyle, false);
    CFRelease(cfpath);
    if (!url) return nullptr;

    // Open image source (supports HEIC, JPEG, PNG, TIFF, …)
    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!src) return nullptr;

    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!img) return nullptr;

    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);

    // Allocate grayscale output buffer (1 byte per pixel)
    uint8_t* pixels = (uint8_t*)malloc(w * h);
    if (!pixels) { CGImageRelease(img); return nullptr; }

    // Create a device-gray bitmap context and draw the image into it.
    // CGBitmapContext automatically converts any source colorspace.
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(
        pixels,                                   // pixel buffer
        w, h,                                     // dimensions
        8,                                        // bits per component
        w,                                        // bytes per row
        gray,
        kCGImageAlphaNone | kCGBitmapByteOrderDefault
    );
    CGColorSpaceRelease(gray);

    if (!ctx) {
        free(pixels);
        CGImageRelease(img);
        return nullptr;
    }

    // Draw — CGBitmapContext origin is bottom-left; flip so top-left = (0,0)
    CGContextTranslateCTM(ctx,  0, (CGFloat)h);
    CGContextScaleCTM    (ctx,  1, -1);
    CGContextDrawImage   (ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    *out_w = (int)w;
    *out_h = (int)h;
    return pixels;
}

// ── Main entry point ──────────────────────────────────────────────────────

int ine_qr_extract_impl(const char* image_path, QRPair* out) {
    // Step 1: load image as grayscale
    int w = 0, h = 0;
    uint8_t* pixels = load_image_gray(image_path, &w, &h);
    if (!pixels) {
        snprintf(out->err, sizeof(out->err),
                 "Cannot load image: %s", image_path);
        out->ok = 0;
        return 0;
    }

    // Step 2: scan for QR codes with zxing-cpp
    ZXing::ImageView view(pixels, w, h, ZXing::ImageFormat::Lum);

    ZXing::ReaderOptions opts;
    opts.setFormats(ZXing::BarcodeFormat::QRCode);
    opts.setTryHarder(true);   // multiple passes for small / dense codes
    opts.setTryRotate(true);   // handle rotated QRs

    auto results = ZXing::ReadBarcodes(view, opts);
    free(pixels);

    if ((int)results.size() < 2) {
        snprintf(out->err, sizeof(out->err),
                 "Expected 2 QR codes, found %d (image: %s)",
                 (int)results.size(), image_path);
        out->ok = 0;
        return 0;
    }

    // Step 3: sort detections left-to-right by top-left corner X coordinate
    std::sort(results.begin(), results.end(),
        [](const ZXing::Barcode& a, const ZXing::Barcode& b) {
            return a.position().topLeft().x < b.position().topLeft().x;
        });

    // Step 4: copy payloads — both must be exactly 858 bytes
    auto copy_payload = [&](const ZXing::Barcode& r,
                             uint8_t* dest, const char* label) -> bool {
        const auto& bytes = r.bytes();
        if ((int)bytes.size() != 858) {
            snprintf(out->err, sizeof(out->err),
                     "%s QR: got %d bytes, expected 858",
                     label, (int)bytes.size());
            return false;
        }
        memcpy(dest, bytes.data(), 858);
        return true;
    };

    if (!copy_payload(results[0], out->left,  "Left"))  { out->ok = 0; return 0; }
    if (!copy_payload(results[1], out->right, "Right")) { out->ok = 0; return 0; }

    out->ok = 1;
    return 1;
}

} // extern "C"
