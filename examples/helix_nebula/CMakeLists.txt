set(shader ${CMAKE_CURRENT_SOURCE_DIR}/helix_nebula.slang)
set(shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(vertex_code ${shader_dir}/helix_nebula.vert.${NOGRAPHICSAPI_SHADER_EXTENSION})
set(fragment_code ${shader_dir}/helix_nebula.frag.${NOGRAPHICSAPI_SHADER_EXTENSION})

NoGraphicsAPI_compile_slang(${vertex_code} ${shader} vertexMain vertex
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/helix_nebula_shared.h)
NoGraphicsAPI_compile_slang(${fragment_code} ${shader} fragmentMain fragment
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/helix_nebula_shared.h)

NoGraphicsAPI_add_example(example_helix_nebula
    helix_nebula.cpp
    helix_nebula_shared.h
    ${shader}
    ${vertex_code}
    ${fragment_code}
    README.md
)
target_link_libraries(example_helix_nebula PRIVATE NoGraphicsAPIUtility::types NoGraphicsAPIUtility::allocators NoGraphicsAPIUtility::uploads)
target_compile_definitions(example_helix_nebula PRIVATE
    NOGRAPHICSAPI_HELIX_VERTEX_SHADER_PATH="${vertex_code}"
    NOGRAPHICSAPI_HELIX_FRAGMENT_SHADER_PATH="${fragment_code}"
)
