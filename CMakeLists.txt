cmake_minimum_required(VERSION 3.31)

project(DotEngine
    VERSION 0.1.0
)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_compile_features(${PROJECT_NAME}
    PRIVATE cxx_std_26
)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    set(COMPILE_COMMANDS_SOURCE
        "${CMAKE_BINARY_DIR}/compile_commands.json"
    )

    set(COMPILE_COMMANDS_TARGET
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    )

    file(CREATE_LINK
        "${COMPILE_COMMANDS_SOURCE}"
        "${COMPILE_COMMANDS_TARGET}"
        SYMBOLIC
        COPY_ON_ERROR
    )
endif()
