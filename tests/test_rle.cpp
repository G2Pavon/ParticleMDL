#include <doctest/doctest.h>

#include <vector>

#include "Exporter.hpp"

using namespace gs;

namespace {

std::size_t entries(const std::vector<short>& v)
{
    return encoded_entries(v);
}

} // namespace

TEST_CASE("encoded_entries drops an all-zero channel")
{
    CHECK(entries({0}) == 0);
    CHECK(entries({0, 0, 0, 0}) == 0);
}

TEST_CASE("encoded_entries keeps a single non-zero value")
{
    // One count entry and the value.
    CHECK(entries({5}) == 2);
}

TEST_CASE("encoded_entries codes a constant channel as one run")
{
    CHECK(entries({7, 7, 7, 7, 7}) == 2);
    CHECK(entries({0, 0, 0}) == 0);   // constant zero is dropped, not coded
}

TEST_CASE("encoded_entries keeps a constant non-zero run past 255 values")
{
    // A run holds at most 255 values: a second count entry and value follow.
    CHECK(entries(std::vector<short>(255, 3)) == 2);
    CHECK(entries(std::vector<short>(256, 3)) == 4);
    CHECK(entries(std::vector<short>(510, 3)) == 4);
    CHECK(entries(std::vector<short>(511, 3)) == 6);
}

TEST_CASE("encoded_entries spends one entry per value on a changing channel")
{
    // One count entry then every value.
    CHECK(entries({1, 2}) == 3);
    CHECK(entries({1, 2, 3}) == 4);
    CHECK(entries({1, 2, 3, 4, 5}) == 6);
}

TEST_CASE("encoded_entries repeats the last value of a stretch inside its count")
{
    // One count entry (6 total, 3 valid) and the values 1, 2, 5: the three
    // trailing 5s are covered by repeating the last valid value.
    CHECK(entries({1, 2, 5, 5, 5, 5}) == 4);
}

TEST_CASE("encoded_entries codes a run followed by a change")
{
    // run of 4 sevens, then 8: [count 4][7][count 1][8] -> 4 entries.
    CHECK(entries({7, 7, 7, 7, 8}) == 4);
}

TEST_CASE("encoded_entries is not fooled by a leading zero followed by data")
{
    CHECK(entries({0, 1}) == 3);
    // Run of three zeros (count + value), then a new count and the 9.
    CHECK(entries({0, 0, 0, 9}) == 4);
}
