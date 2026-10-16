cmake_minimum_required(VERSION 3.31)

if(NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}" OR NOT DEFINED DESTINATION_ROOT)
    message(FATAL_ERROR "Project copy requires source and destination directories")
endif()

# Live editor mailboxes and owned staging are never authored project resources.
# 活跃编辑器邮箱和独立暂存目录不属于项目编辑资源。
# Callers own destination replacement; this helper does not remove existing files.
# 目标整体替换由调用方负责，此辅助过程不删除已有文件。
file(MAKE_DIRECTORY "${DESTINATION_ROOT}")
file(COPY "${SOURCE_ROOT}/" DESTINATION "${DESTINATION_ROOT}"
    PATTERN ".dot-stage-*" EXCLUDE
    PATTERN ".DS_Store" EXCLUDE)
