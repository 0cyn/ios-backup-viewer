find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

qt_standard_project_setup()

add_library(iosbackups_sqliteqt STATIC
    ../../vendor/sqliteQt/qabstractsqlite.cpp
    ../../vendor/sqliteQt/qtablesqlite.cpp
    ../../vendor/sqliteQt/qtreesqlite.cpp
    ../../vendor/sqliteQt/sqliteqt_global.h
    ../../vendor/sqliteQt/qabstractsqlite.h
    ../../vendor/sqliteQt/qtablesqlite.h
    ../../vendor/sqliteQt/qtreesqlite.h
)

target_include_directories(iosbackups_sqliteqt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/sqliteQt
)

target_compile_definitions(iosbackups_sqliteqt PRIVATE SQLITEQT_LIBRARY)

set_target_properties(iosbackups_sqliteqt PROPERTIES AUTOMOC ON)

target_compile_options(iosbackups_sqliteqt PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/widgets/sqliteqt_qt6_compat.h
)

target_link_libraries(iosbackups_sqliteqt PUBLIC Qt6::Sql)

add_library(iosbackups::sqliteqt ALIAS iosbackups_sqliteqt)

add_executable(iosbackups
    MACOSX_BUNDLE

    main.cpp
    mainwindow.cpp
    mainwindow.h
        widgets/backuptree.cpp
        widgets/backuptree.h
        widgets/plisttree.cpp
        widgets/plisttree.h
        widgets/sqlitetree.cpp
        widgets/sqlitetree.h
        widgets/statusbar.cpp
        widgets/statusbar.h
)

target_link_libraries(iosbackups PRIVATE
    Qt6::Widgets
    QHexView
    iosbackups::core
    iosbackups::filetyping
    iosbackups::sqliteqt
)

set_target_properties(iosbackups PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "iosbackups"
    MACOSX_BUNDLE_GUI_IDENTIFIER "me.cynder.iosbackups"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
)
