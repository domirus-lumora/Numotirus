# core/p2p/CMakeLists.txt
add_subdirectory(kcp)
add_subdirectory(kademlia)

add_library(numotirus_p2p STATIC
    p2p.c
)

target_include_directories(numotirus_p2p PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/kcp
    ${CMAKE_CURRENT_SOURCE_DIR}/kademlia
)

target_link_libraries(numotirus_p2p
    kcp
    kademlia
)