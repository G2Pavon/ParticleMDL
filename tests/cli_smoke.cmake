# Runs particlemdl_cli on a small settings file and checks the result.
# Arguments: CLI (the executable), SETTINGS (a .json file), OUT (a scratch folder).

function(run_cli expected_status)
    execute_process(COMMAND ${CLI} ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status STREQUAL expected_status)
        message(FATAL_ERROR "particlemdl_cli ${ARGN}: exit status ${status}, expected ${expected_status}\n${out}${err}")
    endif()
endfunction()

file(REMOVE_RECURSE ${OUT})

# Argument errors
run_cli(2)                                       # nothing given
run_cli(2 --bogus)                               # unknown option
run_cli(2 --settings ${SETTINGS})                # --out missing
run_cli(1 --settings ${OUT}/missing.json --out ${OUT}/x)   # unreadable settings

# A real export; the output folder does not exist yet.
run_cli(0 --settings ${SETTINGS} --out ${OUT}/nested/folder)
foreach(file smoke.qc smoke_ref.smd smoke_anim.smd smoke.bmp)
    if(NOT EXISTS ${OUT}/nested/folder/${file})
        message(FATAL_ERROR "particlemdl_cli did not write ${file}")
    endif()
    file(SIZE ${OUT}/nested/folder/${file} size)
    if(size EQUAL 0)
        message(FATAL_ERROR "${file} is empty")
    endif()
endforeach()
file(READ ${OUT}/nested/folder/smoke.qc qc)
if(NOT qc MATCHES "\\$modelname \"smoke\\.mdl\"")
    message(FATAL_ERROR "smoke.qc has no $modelname line:\n${qc}")
endif()

# An effect over studiomdl's limits is refused before anything is written.
get_filename_component(FIXTURES ${SETTINGS} DIRECTORY)
run_cli(3 --settings ${FIXTURES}/over_limit.json --out ${OUT}/over_limit)
if(EXISTS ${OUT}/over_limit)
    message(FATAL_ERROR "particlemdl_cli created a folder for an export it refused")
endif()

# A compiler that does not exist is reported, not ignored. (On Windows compiling
# outside the current folder is refused before the compiler is looked up.)
if(NOT CMAKE_HOST_WIN32)
    run_cli(4 --settings ${SETTINGS} --out ${OUT}/nocompiler --compile ${OUT}/no-such-studiomdl)
endif()

file(REMOVE_RECURSE ${OUT})
