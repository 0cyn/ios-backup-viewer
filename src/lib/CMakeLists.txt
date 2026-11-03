add_library(plistcpp STATIC
    ../../vendor/plistcpp/src/Plist.cpp
    ../../vendor/plistcpp/src/PlistDate.cpp
    ../../vendor/plistcpp/src/pugixml.cpp
)

target_include_directories(plistcpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/plistcpp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/plistcpp/include
)

add_library(iosbackups::plistcpp ALIAS plistcpp)

add_library(iosbackups_core STATIC
        Backup.cpp
        include/Backup.h
)

add_library(iosbackups::core ALIAS iosbackups_core)

find_package(SQLite3 REQUIRED)

target_include_directories(iosbackups_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(iosbackups_core PUBLIC cxx_std_20)

target_link_libraries(iosbackups_core PUBLIC
    plistcpp
)

target_link_libraries(iosbackups_core PRIVATE
    SQLite::SQLite3
)

find_path(MAGIC_INCLUDE_DIR magic.h REQUIRED)
find_library(MAGIC_LIBRARY magic REQUIRED)

add_library(iosbackups_filetyping STATIC
    ../../vendor/file_typing/Magic.cpp
    ../../vendor/file_typing/Magic.hpp
)

target_include_directories(iosbackups_filetyping PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/file_typing
    ${MAGIC_INCLUDE_DIR}
)

target_link_libraries(iosbackups_filetyping PUBLIC
    ${MAGIC_LIBRARY}
)

add_library(iosbackups::filetyping ALIAS iosbackups_filetyping)
