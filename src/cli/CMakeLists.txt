add_executable(iosbackups-cli
    main.cpp
)

target_link_libraries(iosbackups-cli PRIVATE
    iosbackups::core
)
