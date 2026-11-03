# 业务逻辑单独编成库，服务端使用 SQL 模型，测试程序可提供相同接口的内存模型。
add_library(chat_business chatserver.cpp chatservice.cpp)
add_executable(ChatServer main.cpp demo_runtime.cpp db/db.cpp
    table_model/usermodel.cpp table_model/friendmodel.cpp
    table_model/groupmodel.cpp table_model/offlinemessagemodel.cpp)
target_include_directories(chat_business PUBLIC
    ${PROJECT_SOURCE_DIR}/include/server ${PROJECT_SOURCE_DIR}/include/server/db
    ${PROJECT_SOURCE_DIR}/include/server/table_model ${PROJECT_SOURCE_DIR}/include/server/ORM_class
    ${PROJECT_SOURCE_DIR}/thirdparty ${MYSQL_INCLUDE_DIR})
target_link_libraries(chat_business PUBLIC chat_udp)
target_link_libraries(ChatServer PRIVATE chat_business ${MYSQL_LIBRARY})
if(EXISTS "${PROJECT_SOURCE_DIR}/.env")
    set_property(TARGET ChatServer APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/.env")
    add_custom_command(TARGET ChatServer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/.env" "$<TARGET_FILE_DIR:ChatServer>/.env")
endif()
if(WIN32)
    # 动态链接 MySQL 时，将客户端及其 TLS 依赖复制到可执行文件所在目录。
    get_filename_component(MYSQL_LIBRARY_DIR "${MYSQL_LIBRARY}" DIRECTORY)
    if(EXISTS "${MYSQL_LIBRARY_DIR}/libmysql.dll")
        add_custom_command(TARGET ChatServer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MYSQL_LIBRARY_DIR}/libmysql.dll" "$<TARGET_FILE_DIR:ChatServer>")
        file(GLOB MYSQL_TLS_DLLS
            "${MYSQL_LIBRARY_DIR}/libssl*.dll" "${MYSQL_LIBRARY_DIR}/libcrypto*.dll"
            "${MYSQL_LIBRARY_DIR}/../bin/libssl*.dll" "${MYSQL_LIBRARY_DIR}/../bin/libcrypto*.dll")
        foreach(MYSQL_TLS_DLL IN LISTS MYSQL_TLS_DLLS)
            add_custom_command(TARGET ChatServer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MYSQL_TLS_DLL}" "$<TARGET_FILE_DIR:ChatServer>")
        endforeach()
    endif()
endif()
