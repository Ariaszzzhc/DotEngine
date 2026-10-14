cmake_minimum_required(VERSION 3.31)
set(dot_header "#pragma once\n\nnamespace Dot::Graphics::Detail {\n")
foreach(dot_name IN ITEMS Surface ToneMapping Environment SurfaceDirect SurfaceEnvironment Sky
    ShadowDepth VisibilityDirect VisibilityShadows Geometry Occlusion OcclusionFilter
    AmbientOcclusion AmbientUnoccluded)
    file(READ "${INPUT}/${dot_name}.wgsl" dot_source)
    string(FIND "${dot_source}" ")dot_wgsl\"" dot_delimiter)
    if(NOT dot_delimiter EQUAL -1)
        message(FATAL_ERROR "${dot_name}.wgsl contains the embedding delimiter")
    endif()
    string(SUBSTRING "${dot_name}" 0 1 dot_first)
    string(TOLOWER "${dot_first}" dot_first)
    string(SUBSTRING "${dot_name}" 1 -1 dot_rest)
    string(APPEND dot_header
        "  inline constexpr auto ${dot_first}${dot_rest}Shader = R\"dot_wgsl(${dot_source})dot_wgsl\";\n\n")
endforeach()
string(APPEND dot_header "}  // namespace Dot::Graphics::Detail\n")
file(WRITE "${OUTPUT}" "${dot_header}")
