add_executable(particlemdl_tests
    main.cpp
    fake_studiomdl.cpp
    test_compiler.cpp
    test_math.cpp
    test_life_cycle.cpp
    test_studio_angles.cpp
    test_rle.cpp
    test_settings.cpp
    test_settings_json.cpp
    test_model_name.cpp
    test_path_input.cpp
    test_baking.cpp
    test_export_blockers.cpp
    test_staged_files.cpp
    test_image_import.cpp
    test_studiomdl_verdicts.cpp
    test_mdl_reader.cpp
    test_studiomdl_numbers.cpp
    test_texture_job.cpp
    test_corner_bones.cpp
    test_emitter_shapes.cpp
    test_one_shot.cpp
    test_randomness.cpp
    test_additive.cpp
    test_bone_layout.cpp
    test_seed_roll.cpp
    test_clusters.cpp
    test_align.cpp
    test_bounce.cpp
    test_presets.cpp
    test_utf8_paths.cpp
    test_golden.cpp)
target_link_libraries(particlemdl_tests PRIVATE particles_editor_support doctest gs_flags)
target_compile_definitions(particlemdl_tests PRIVATE GS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    GS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# The golden snapshots were recorded on Linux/glibc. libm is not bit-exact
# across platforms (audit M9), so elsewhere the golden tests are left out.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME unit COMMAND particlemdl_tests)
else()
    add_test(NAME unit COMMAND particlemdl_tests "--test-case-exclude=golden*")
endif()

# Command-line exporter: argument handling, then a real export of a small fixture.
add_test(NAME cli_help COMMAND particlemdl_cli --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: particlemdl_cli")
add_test(NAME cli_version COMMAND particlemdl_cli --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "particlemdl_cli [0-9]+\\.[0-9]+")
add_test(NAME cli_export
    COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:particlemdl_cli>
        -DSETTINGS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/minimal.json
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/cli_export
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake)
