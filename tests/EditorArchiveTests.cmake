file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}/DotEngine/nested/empty" "${ROOT}/extract")
file(WRITE "${ROOT}/DotEngine/.DS_Store" "root metadata")
file(WRITE "${ROOT}/DotEngine/nested/.DS_Store" "nested metadata")
file(WRITE "${ROOT}/DotEngine/payload.txt" "Preserve payload\n")
file(WRITE "${ROOT}/DotEngine/nested/notice.txt" "Preserve nested payload\n")
file(WRITE "${ROOT}/DotEngine/executable" "#!/bin/sh\nexit 42\n")
file(CHMOD "${ROOT}/DotEngine/executable" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(CREATE_LINK "payload.txt" "${ROOT}/DotEngine/alias" SYMBOLIC RESULT linked)
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIRECTORY=${ROOT}/DotEngine"
    "-DARCHIVE=${ROOT}/editor.zip" "-DSTAGE_DIRECTORY=${ROOT}/.dot-editor-archive-stage" -P "${ARCHIVE_SCRIPT}"
    RESULT_VARIABLE archived)
if(NOT archived EQUAL 0 OR EXISTS "${ROOT}/.dot-editor-archive-stage")
    message(FATAL_ERROR "Archive creation/owned staging cleanup failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar tf "${ROOT}/editor.zip"
    OUTPUT_VARIABLE entries RESULT_VARIABLE listed)
if(NOT listed EQUAL 0 OR entries MATCHES "\\.DS_Store")
    message(FATAL_ERROR "Finder metadata leaked into archive")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${ROOT}/editor.zip"
    WORKING_DIRECTORY "${ROOT}/extract" RESULT_VARIABLE extracted)
if(NOT extracted EQUAL 0 OR NOT IS_DIRECTORY "${ROOT}/extract/DotEngine/nested/empty")
    message(FATAL_ERROR "Archive extraction/empty directory preservation failed")
endif()
file(READ "${ROOT}/extract/DotEngine/payload.txt" payload)
file(READ "${ROOT}/extract/DotEngine/nested/notice.txt" notice)
if(NOT payload STREQUAL "Preserve payload\n" OR NOT notice STREQUAL "Preserve nested payload\n"
        OR NOT EXISTS "${ROOT}/DotEngine/.DS_Store"
        OR NOT EXISTS "${ROOT}/DotEngine/nested/.DS_Store")
    message(FATAL_ERROR "Payload changed or source metadata was destructively modified")
endif()
if(linked STREQUAL "0" AND NOT IS_SYMLINK "${ROOT}/extract/DotEngine/alias")
    message(FATAL_ERROR "Relative symlink was not preserved")
endif()
if(NOT WIN32)
    execute_process(COMMAND "${ROOT}/extract/DotEngine/executable" RESULT_VARIABLE executed)
    if(NOT executed EQUAL 42)
        message(FATAL_ERROR "Executable permissions were not preserved")
    endif()
endif()
file(SHA256 "${ROOT}/editor.zip" before_failure)
foreach(invalid_stage IN ITEMS "${ROOT}/DotEngine" "${ROOT}/DotEngine/.dot-editor-archive-stage")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIRECTORY=${ROOT}/DotEngine"
        "-DARCHIVE=${ROOT}/editor.zip" "-DSTAGE_DIRECTORY=${invalid_stage}" -P "${ARCHIVE_SCRIPT}"
        RESULT_VARIABLE refused OUTPUT_QUIET ERROR_QUIET)
    file(SHA256 "${ROOT}/editor.zip" after_failure)
    if(refused EQUAL 0 OR NOT before_failure STREQUAL after_failure
            OR NOT EXISTS "${ROOT}/DotEngine/payload.txt")
        message(FATAL_ERROR "Invalid staging altered the source or previous published archive")
    endif()
endforeach()
file(REMOVE_RECURSE "${ROOT}")
