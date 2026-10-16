cmake_minimum_required(VERSION 3.31)

if(NOT DEFINED ROOT OR NOT DEFINED COPY_SCRIPT)
    message(FATAL_ERROR "Project-copy test requires owned storage and the copy script")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}/source/scenes" "${ROOT}/source/.dot-stage-mailbox/requests"
    "${ROOT}/source/assets/.dot-stage-model")
file(WRITE "${ROOT}/source/scenes/level.dotscene" "authored scene")
file(WRITE "${ROOT}/source/.DS_Store" "finder metadata")
file(WRITE "${ROOT}/source/scenes/.DS_Store" "nested metadata")
file(WRITE "${ROOT}/source/.dot-stage-mailbox/requests/request.json" "private request")
file(WRITE "${ROOT}/source/assets/.dot-stage-model/partial.dotmesh" "partial model")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${ROOT}/source"
    "-DDESTINATION_ROOT=${ROOT}/copy" -P "${COPY_SCRIPT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Project copy failed: ${output}${error}")
endif()

file(READ "${ROOT}/copy/scenes/level.dotscene" scene)

if(NOT scene STREQUAL "authored scene")
    message(FATAL_ERROR "Project copy lost authored scene content")
endif()

foreach(path ".DS_Store" "scenes/.DS_Store" ".dot-stage-mailbox" "assets/.dot-stage-model")
    if(EXISTS "${ROOT}/copy/${path}")
        message(FATAL_ERROR "Project copy included private or host data: ${path}")
    endif()
endforeach()

if(NOT EXISTS "${ROOT}/source/.dot-stage-mailbox/requests/request.json")
    message(FATAL_ERROR "Project copy modified the live source")
endif()

file(REMOVE_RECURSE "${ROOT}")
