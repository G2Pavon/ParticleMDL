#include <doctest/doctest.h>

#include "OrbitCamera.hpp"

using namespace gs;

TEST_CASE("the zoom factor is 1 in the default view and grows as the camera comes closer")
{
    OrbitCamera camera;
    CHECK(camera.zoom_factor() == doctest::Approx(1.0));
    camera.zoom(1);   // one notch in: 50 units closer
    CHECK(camera.distance() == doctest::Approx(250.0));
    CHECK(camera.zoom_factor() == doctest::Approx(1.2));

    for (int i = 0; i < 100; ++i)
        camera.zoom(1);
    CHECK(camera.zoom_factor() == doctest::Approx(6.0));   // as close as it goes
    for (int i = 0; i < 100; ++i)
        camera.zoom(-1);
    CHECK(camera.zoom_factor() == doctest::Approx(0.1));   // as far as it goes

    camera.reset();
    CHECK(camera.zoom_factor() == doctest::Approx(1.0));
}
