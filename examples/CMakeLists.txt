include(NoGraphicsAPI_slang.cmake)

add_library(NoGraphicsAPI_example_support STATIC
    common/example_support.cpp
    common/example_support.hpp
)
if(APPLE)
    target_sources(NoGraphicsAPI_example_support PRIVATE common/example_window_macos.mm)
    target_link_libraries(NoGraphicsAPI_example_support PRIVATE "-framework AppKit" "-framework QuartzCore")
    target_compile_options(NoGraphicsAPI_example_support PRIVATE -fno-objc-arc)
elseif(WIN32)
    target_sources(NoGraphicsAPI_example_support PRIVATE common/example_window_win32.cpp)
else()
    enable_language(C)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Wayland REQUIRED IMPORTED_TARGET wayland-client>=1.22)
    pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols)
    pkg_get_variable(wayland_protocols_dir wayland-protocols pkgdatadir)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
    set(xdg_shell_xml ${wayland_protocols_dir}/stable/xdg-shell/xdg-shell.xml)
    set(xdg_shell_header ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h)
    set(xdg_shell_code ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
    add_custom_command(OUTPUT ${xdg_shell_header} ${xdg_shell_code}
        COMMAND ${WAYLAND_SCANNER} client-header ${xdg_shell_xml} ${xdg_shell_header}
        COMMAND ${WAYLAND_SCANNER} private-code ${xdg_shell_xml} ${xdg_shell_code}
        DEPENDS ${xdg_shell_xml}
        VERBATIM)
    add_library(NoGraphicsAPI_xdg_shell STATIC ${xdg_shell_code} ${xdg_shell_header})
    target_link_libraries(NoGraphicsAPI_xdg_shell PUBLIC PkgConfig::Wayland)
    target_include_directories(NoGraphicsAPI_xdg_shell PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    target_sources(NoGraphicsAPI_example_support PRIVATE common/example_window_linux.cpp common/example_window_wayland.hpp)
    target_link_libraries(NoGraphicsAPI_example_support PRIVATE NoGraphicsAPI_xdg_shell)
endif()
NoGraphicsAPI_disable_exceptions(NoGraphicsAPI_example_support)
target_link_libraries(NoGraphicsAPI_example_support PUBLIC NoGraphicsAPI::NoGraphicsAPI)
target_include_directories(NoGraphicsAPI_example_support PUBLIC common)
if(WIN32)
    target_link_libraries(NoGraphicsAPI_example_support PUBLIC user32)
    target_compile_definitions(NoGraphicsAPI_example_support PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(triangle)
add_subdirectory(cube)
add_subdirectory(deferred_renderer)
add_subdirectory(advanced_deferred_renderer)

function(NoGraphicsAPI_make_release_gui target)
    if(WIN32)
        set_target_properties(${target} PROPERTIES
            WIN32_EXECUTABLE "$<$<CONFIG:Release>:TRUE>"
        )
        target_link_options(${target} PRIVATE
            "$<$<CONFIG:Release>:/ENTRY:mainCRTStartup>"
        )
    endif()
endfunction()

NoGraphicsAPI_make_release_gui(example_triangle)
NoGraphicsAPI_make_release_gui(example_cube)
NoGraphicsAPI_make_release_gui(example_deferred_renderer)
NoGraphicsAPI_make_release_gui(example_advanced_deferred_renderer)
