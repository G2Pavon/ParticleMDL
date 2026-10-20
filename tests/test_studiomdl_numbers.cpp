#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>

#include "Exporter.hpp"

using namespace gs;

namespace {

// What studiomdl does with a number in an SMD: the text "%.6f" and the float read from it.
float by_text(double v)
{
    if (std::abs(v) < 5e-7)
        return 0.0f;
    std::array<char, 64> buf;
    const auto written = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, 6);
    float f = 0;
    std::from_chars(buf.data(), written.ptr, f);
    return f;
}

struct Rng {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint64_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    double unit() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }   // [0, 1)
};

} // namespace

TEST_CASE("as_read_by_studiomdl gives the float that printing and reading the text gives")
{
    Rng rng;
    long checked = 0;
    auto check = [&](double v) {
        const float fast = as_read_by_studiomdl(v), text = by_text(v);
        ++checked;
        if (std::bit_cast<std::uint32_t>(fast) != std::bit_cast<std::uint32_t>(text)) {
            INFO("value " << v << ": " << fast << " but the text gives " << text);
            CHECK(fast == text);   // fails, and says which value
        }
    };

    // Values of the sizes animations have: positions in the hundreds, angles in radians, tiny ones.
    for (int i = 0; i < 1'500'000; ++i) {
        const double magnitude = std::pow(10.0, -7.0 + 12.0 * rng.unit());   // 1e-7 .. 1e5
        check((rng.next() & 1 ? 1.0 : -1.0) * magnitude * rng.unit());
        check((rng.next() & 1 ? 1.0 : -1.0) * (rng.unit() * 512.0));
    }
    // Rounding ties and their neighbours: the decimal digits are where the two ways could differ.
    for (int i = 0; i < 600'000; ++i) {
        const double k = std::floor(rng.unit() * 2e8) - 1e8;
        const double tie = (k + 0.5) / 1e6;
        check(tie);
        check(std::nextafter(tie, 1e9));
        check(std::nextafter(tie, -1e9));
        check(tie * (1.0 + 1e-12));
        check(tie * (1.0 - 1e-12));
    }
    // Exact multiples of 1e-6, powers of two, the edges of the cases.
    for (int i = -3000; i <= 3000; ++i) {
        check(i / 1e6);
        check(i / 64.0);
        check(i * 0.1);
    }
    for (const double v : {0.0, -0.0, 5e-7, -5e-7, 4.9999999e-7, 1e-6, 0.5e-6 * 3, 99999.9999995, 100000.0, -100000.0,
                           100000.5, 1e6, 123456789.123456789, 65535.0, -128.0, 128.0, 3.141592653589793, -3.141592653589793,
                           1.5707963267948966, 6.283185307179586})
        check(v);
    CHECK(checked > 4'000'000);
}

TEST_CASE("as_read_by_studiomdl returns zero for what prints as zero")
{
    CHECK(as_read_by_studiomdl(0.0) == 0.0f);
    CHECK(as_read_by_studiomdl(4e-7) == 0.0f);
    CHECK(as_read_by_studiomdl(-4e-7) == 0.0f);
    CHECK(as_read_by_studiomdl(1e-6) == 1e-6f);
}
