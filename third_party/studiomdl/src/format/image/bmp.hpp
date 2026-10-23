#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// __attribute__((packed)) on non-Intel arch may cause some unexpected error, plz be informed.
#pragma pack(push, 1)
struct BITMAPFILEHEADER
{
    uint16_t bfType;      // 2  /* Magic identifier */
    uint32_t bfSize;      // 4  /* File size in bytes */
    uint16_t bfReserved1; // 2
    uint16_t bfReserved2; // 2
    uint32_t bfOffBits;   // 4 /* Offset to image data, bytes */
};
#pragma pack(pop)

enum Compression
{
    BI_RGB = 0x0000,
    BI_RLE8 = 0x0001,
    BI_RLE4 = 0x0002,
    BI_BITFIELDS = 0x0003,
    BI_JPEG = 0x0004,
    BI_PNG = 0x0005,
    BI_CMYK = 0x000B,
    BI_CMYKRLE8 = 0x000C,
    BI_CMYKRLE4 = 0x000D
};

struct BITMAPINFOHEADER
{
    uint32_t biSize;         // 4 /* Header size in bytes */
    int32_t biWidth;         // 4 /* Width of image */
    int32_t biHeight;        // 4 /* Height of image */
    uint16_t biPlanes;       // 2 /* Number of colour planes */
    uint16_t biBitCount;     // 2 /* Bits per pixel */
    uint32_t biCompression;  // 4 /* Compression type */
    uint32_t biSizeImage;    // 4 /* Image size in bytes */
    int32_t biXPelsPerMeter; // 4
    int32_t biYPelsPerMeter; // 4 /* Pixels per meter */
    uint32_t biClrUsed;      // 4 /* Number of colours */
    uint32_t biClrImportant; // 4 /* Important colours */
};

struct RGBQUAD
{
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
};
// for biBitCount is 16/24/32, it may be useless

// Where the 8-bit pixel rows of a validated BMP live inside the file. Rows are
// stored bottom-up, each `stride` bytes (the width rounded up to 4).
struct BmpLayout
{
    std::size_t pixel_offset = 0;
    std::size_t stride = 0;

    // Row `y` counted from the top of the image.
    const std::uint8_t *row(std::span<const std::uint8_t> file, int height, int y) const
    {
        return file.data() + pixel_offset + static_cast<std::size_t>(height - 1 - y) * stride;
    }
};

// Validates an uncompressed 8-bit BMP held in memory without copying its pixels.
// On success returns 0 and fills `layout`, the 768-byte RGB `palette` and the
// size; on failure returns a negative code and leaves the outputs alone.
int load_bmp(std::span<const std::uint8_t> file, BmpLayout &layout,
             std::array<std::uint8_t, 768> &palette, int *width, int *height);