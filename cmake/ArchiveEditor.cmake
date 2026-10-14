# Snapshot generated distribution contents before archiving: Finder may recreate
# metadata in an open distribution folder even after the build removes it.
# 从生成发行目录创建快照再压缩：Finder 可能在构建删除后重建打开目录中的元数据。
if(NOT IS_DIRECTORY "${SOURCE_DIRECTORY}" OR IS_SYMLINK "${SOURCE_DIRECTORY}"
        OR NOT IS_ABSOLUTE "${ARCHIVE}" OR NOT IS_ABSOLUTE "${STAGE_DIRECTORY}"
        OR STAGE_DIRECTORY STREQUAL SOURCE_DIRECTORY)
    message(FATAL_ERROR "Invalid editor archive source/output/staging directory")
endif()
get_filename_component(stage_name "${STAGE_DIRECTORY}" NAME)
cmake_path(IS_PREFIX SOURCE_DIRECTORY "${STAGE_DIRECTORY}" NORMALIZE stage_in_source)
cmake_path(IS_PREFIX STAGE_DIRECTORY "${SOURCE_DIRECTORY}" NORMALIZE source_in_stage)
cmake_path(IS_PREFIX STAGE_DIRECTORY "${ARCHIVE}" NORMALIZE output_in_stage)
if(NOT stage_name STREQUAL ".dot-editor-archive-stage" OR IS_SYMLINK "${STAGE_DIRECTORY}"
        OR stage_in_source OR source_in_stage OR output_in_stage)
    message(FATAL_ERROR "Editor archive staging must be a separate owned directory")
endif()
file(REMOVE_RECURSE "${STAGE_DIRECTORY}")
file(MAKE_DIRECTORY "${STAGE_DIRECTORY}")
file(COPY "${SOURCE_DIRECTORY}" DESTINATION "${STAGE_DIRECTORY}"
    USE_SOURCE_PERMISSIONS PATTERN ".DS_Store" EXCLUDE)
get_filename_component(package_name "${SOURCE_DIRECTORY}" NAME)
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf archive.zip --format=zip "${package_name}"
    WORKING_DIRECTORY "${STAGE_DIRECTORY}" RESULT_VARIABLE archived)
if(NOT archived EQUAL 0)
    file(REMOVE_RECURSE "${STAGE_DIRECTORY}")
    message(FATAL_ERROR "Archive editor distribution failed: ${archived}")
endif()
file(RENAME "${STAGE_DIRECTORY}/archive.zip" "${ARCHIVE}" RESULT published)
file(REMOVE_RECURSE "${STAGE_DIRECTORY}")
if(NOT published STREQUAL "0")
    message(FATAL_ERROR "Publish editor archive failed: ${published}")
endif()
