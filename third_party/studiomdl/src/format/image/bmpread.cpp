#include "format/image/bmp.hpp"

#include <cstdint>
#include <cstring>

namespace
{
// Sequential reader over the file bytes; every read is bounds-checked.
class ByteReader
{
public:
	explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

	bool read(void *out, std::size_t count)
	{
		if (count > data_.size() - pos_)
			return false;
		std::memcpy(out, data_.data() + pos_, count);
		pos_ += count;
		return true;
	}

	std::size_t position() const { return pos_; }

private:
	std::span<const std::uint8_t> data_;
	std::size_t pos_ = 0;
};
} // namespace

int load_bmp(std::span<const std::uint8_t> file, BmpLayout &layout,
			 std::array<std::uint8_t, 768> &palette, int *width, int *height)
{
	ByteReader reader{file};
	BITMAPFILEHEADER bmfh;
	BITMAPINFOHEADER bmih;
	RGBQUAD rgrgbPalette[256];
	uint32_t cbPalBytes;

	// Read file header
	if (!reader.read(&bmfh, sizeof bmfh))
		return -2;

	// Bogus file header check
	if (!(bmfh.bfReserved1 == 0 && bmfh.bfReserved2 == 0))
		return -2000;

	// Read info header
	if (!reader.read(&bmih, sizeof bmih))
		return -3;

	// Bogus info header check
	if (!(bmih.biSize == sizeof bmih && bmih.biPlanes == 1))
		return -3000;

	// Bogus bit depth?  Only 8-bit supported.
	if (bmih.biBitCount != 8)
		return -4;

	// Bogus compression?  Only non-compressed supported.
	if (bmih.biCompression != BI_RGB)
		return -5;

	// Only bottom-up images with a sane size are supported.
	if (bmih.biWidth <= 0 || bmih.biHeight <= 0 || bmih.biWidth > 0xFFFF || bmih.biHeight > 0xFFFF)
		return -3000;

	// Figure out how many entries are actually in the table
	if (bmih.biClrUsed == 0)
	{
		bmih.biClrUsed = 256;
		cbPalBytes = (1 << bmih.biBitCount) * sizeof(RGBQUAD);
	}
	else if (bmih.biClrUsed > 256)
	{
		return -6;
	}
	else
	{
		cbPalBytes = bmih.biClrUsed * sizeof(RGBQUAD);
	}

	// Read palette (bmih.biClrUsed entries)
	if (!reader.read(rgrgbPalette, cbPalBytes))
		return -6;

	// Read bitmap bits (remainder of file, as declared by the header)
	if (bmfh.bfSize < reader.position())
		return -7;
	const uint32_t cbBmpBits = bmfh.bfSize - static_cast<uint32_t>(reader.position());

	// Data is actually stored with the width being rounded up to a multiple of 4
	const uint32_t biTrueWidth = (static_cast<uint32_t>(bmih.biWidth) + 3) & ~3u;
	if (static_cast<uint64_t>(biTrueWidth) * static_cast<uint32_t>(bmih.biHeight) > cbBmpBits)
		return -7;

	std::span<const std::uint8_t> bits = file.subspan(reader.position());
	if (bits.size() < cbBmpBits)
		return -7;

	// Convert to a packed 768-byte palette
	uint8_t *pb = palette.data();

	// Copy over used entries
	for (int i = 0; i < static_cast<int>(bmih.biClrUsed); i++)
	{
		*pb++ = rgrgbPalette[i].rgbRed;
		*pb++ = rgrgbPalette[i].rgbGreen;
		*pb++ = rgrgbPalette[i].rgbBlue;
	}

	// Fill in unused entries with 0,0,0
	for (int i = bmih.biClrUsed; i < 256; i++)
	{
		*pb++ = 0;
		*pb++ = 0;
		*pb++ = 0;
	}

	layout.pixel_offset = reader.position();
	layout.stride = biTrueWidth;

	*width = (uint16_t)bmih.biWidth;
	*height = (uint16_t)bmih.biHeight;

	return 0;
}
