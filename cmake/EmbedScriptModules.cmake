cmake_minimum_required(VERSION 3.31)
file(READ "${INPUT}/engine.js" dot_engine_source)
file(READ "${INPUT}/engine.d.ts" dot_engine_types)
file(READ "${HOST}" dot_host_types)
# Generated declarations become one ambient module, not project-owned implementation.
# 生成声明包装成一个环境模块，而不是项目拥有的实现。
string(REPLACE "export declare " "export " dot_engine_types "${dot_engine_types}")
set(dot_engine_types "declare module 'dot:engine' {\n${dot_engine_types}\n}\n")
foreach(dot_content IN ITEMS dot_engine_source dot_engine_types dot_host_types)
    string(FIND "${${dot_content}}" ")dot_script\"" dot_delimiter)
    if(NOT dot_delimiter EQUAL -1)
        message(FATAL_ERROR "Engine script contains the embedding delimiter")
    endif()
endforeach()
file(WRITE "${OUTPUT}" "#pragma once\n\nnamespace Dot::Script::Detail {\n"
    "inline constexpr auto engineSource = R\"dot_script(${dot_engine_source})dot_script\";\n"
    "inline constexpr auto engineTypes = R\"dot_script(${dot_engine_types})dot_script\";\n"
    "inline constexpr auto hostTypes = R\"dot_script(${dot_host_types})dot_script\";\n}\n")
