#include <doctest/doctest.h>

#include <cmath>

#include "MathUtil.hpp"

using namespace gs;

TEST_CASE("floor_mod wraps into [0, period) for a positive period")
{
    CHECK(floor_mod(5.0, 3.0) == doctest::Approx(2.0));
    CHECK(floor_mod(-1.0, 3.0) == doctest::Approx(2.0));
    CHECK(floor_mod(-7.5, 3.0) == doctest::Approx(1.5));
    CHECK(floor_mod(0.5, 3.0) == doctest::Approx(0.5));
    CHECK(floor_mod(1e9 + 0.25, 1.0) == doctest::Approx(0.25));
}

TEST_CASE("floor_mod of an exact multiple is a zero with the divisor's sign")
{
    CHECK(floor_mod(6.0, 3.0) == 0.0);
    CHECK_FALSE(std::signbit(floor_mod(6.0, 3.0)));
    CHECK_FALSE(std::signbit(floor_mod(-6.0, 3.0)));
    CHECK(std::signbit(floor_mod(6.0, -3.0)));
}

TEST_CASE("floor_mod takes the sign of the divisor")
{
    CHECK(floor_mod(1.0, -3.0) == doctest::Approx(-2.0));
    CHECK(floor_mod(-1.0, -3.0) == doctest::Approx(-1.0));
}

TEST_CASE("floor_mod result is always inside the period")
{
    for (double a = -20.0; a <= 20.0; a += 0.37) {
        const double m = floor_mod(a, 2.5);
        CHECK(m >= 0.0);
        CHECK(m < 2.5);
    }
}
