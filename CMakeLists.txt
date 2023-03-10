cmake_minimum_required(VERSION 3.1)

project(ComputeShaderPlayground)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

file(GLOB src_files "./src/*")
file(GLOB _shader "./src/shaders/*")
file(GLOB _dlls "./lib/*")
list(FILTER src_files INCLUDE REGEX "\\.cpp|\\.h")
list(FILTER _shader INCLUDE REGEX "\\.hlsl")
list(FILTER _dlls INCLUDE REGEX "\\.dll")

set_source_files_properties(${_shader} PROPERTIES VS_SHADER_TYPE Compute VS_SHADER_MODEL 6.2)

get_filename_component(_name "SimpleComputeShader" NAME)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:Console")
set(VS_STARTUP_PROJECT ${_name})


add_executable(${_name} WIN32 ${src_files})
target_compile_definitions(${_name} PRIVATE "UNICODE" "_UNICODE")
target_link_libraries(${_name} PRIVATE "d3d12.lib" "dxgi.lib" "d3dcompiler.lib" "dxcompiler.lib")

set_target_properties(${_name} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_name}>"
                                               VS_DEBUGGER_COMMAND           "$<TARGET_FILE:${_name}>")

add_custom_command(
    TARGET ${_name}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy ${_shader} $<TARGET_FILE_DIR:${_name}>
    COMMAND "${CMAKE_COMMAND}" -E copy ${_dlls} $<TARGET_FILE_DIR:${_name}>
)