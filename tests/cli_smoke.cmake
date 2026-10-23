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

# A real export; the output folder does not exist yet. Only the .mdl is written.
run_cli(0 --settings ${SETTINGS} --out ${OUT}/nested/folder)
file(GLOB written RELATIVE ${OUT}/nested/folder ${OUT}/nested/folder/*)
if(NOT written STREQUAL "smoke.mdl")
    message(FATAL_ERROR "particlemdl_cli wrote '${written}', not just smoke.mdl")
endif()
file(READ ${OUT}/nested/folder/smoke.mdl magic LIMIT 4 HEX)
if(NOT magic STREQUAL "49445354")   # "IDST"
    message(FATAL_ERROR "smoke.mdl does not start like a studio model")
endif()

# An effect over studiomdl's limits is refused before anything is written.
get_filename_component(FIXTURES ${SETTINGS} DIRECTORY)
run_cli(3 --settings ${FIXTURES}/over_limit.json --out ${OUT}/over_limit)
if(EXISTS ${OUT}/over_limit)
    message(FATAL_ERROR "particlemdl_cli created a folder for an export it refused")
endif()

run_cli(2 --settings ${SETTINGS} --out ${OUT}/x --compile)   # compiling is not an option any more

file(REMOVE_RECURSE ${OUT})
