add_executable(particlemdl_tests
    main.cpp
    test_math.cpp
    test_life_cycle.cpp
    test_studio_angles.cpp
    test_rle.cpp
    test_settings.cpp
    test_golden.cpp)
target_link_libraries(particlemdl_tests PRIVATE particles_core doctest gs_flags)
target_compile_definitions(particlemdl_tests PRIVATE GS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME unit COMMAND particlemdl_tests)
