# Standalone helper that exports Realmz tilesets to PNG for the Tiled editor.
# Links only the resource decoder libs (no SDL / engine), so it builds quickly.
add_executable(tileset_dump main.cpp)
target_link_libraries(tileset_dump
    phosg::phosg
    resource_file::resource_file
    z
)
if(WIN32)
    target_link_libraries(tileset_dump bcrypt)
endif()
