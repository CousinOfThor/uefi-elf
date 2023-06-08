#include <stdint.h>

#define _STDINT_H 1 // otherwise clang complains about int8_t redefinition in UEFI.h
#include "UEFI.h"

void main(efi_gop_t *gop) {
    void *address = (void *) gop->Mode->FrameBufferBase;
    uint32_t width = gop->Mode->Information->HorizontalResolution;
    uint32_t stride = gop->Mode->Information->PixelsPerScanLine;
    uint32_t y = 50;
    uint32_t channels = 4; // rgba or bgra
    for (uint32_t x = 0; x < width; x++) {
        uint32_t *row = address + y * channels * stride;
        row[x] = 0xff0000ff;
    }

    while (1) {};
}
