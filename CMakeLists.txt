cmake_minimum_required(VERSION 3.16)

project(Lan_Share VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)



# 1. Знаходимо пакети Qt
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Quick Qml)

# 2. !!! ВИПРАВЛЕННЯ ПОМИЛКИ VS (QTP0001) !!!
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)

# 3. Список файлів C++
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    discoverymanager.cpp
    discoverymanager.h
    filetransfermanager.cpp
    filetransfermanager.h
)

# 4. Створення виконуваного файлу
qt_add_executable(Lan_Share ${PROJECT_SOURCES})

# 5. Додавання QML модуля (ресурсів)
qt_add_qml_module(Lan_Share
    URI Lan_Share
    VERSION 1.0
    QML_FILES main.qml
)

# 6. Лінковка бібліотек
target_link_libraries(Lan_Share PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Quick
    Qt6::Qml
)

# 7. Налаштування для Windows (ховає консоль, якщо треба)
set_target_properties(Lan_Share PROPERTIES WIN32_EXECUTABLE TRUE)

# 8. Налаштування для Android (спрацює тільки при збірці під Android)
if(ANDROID)
    set_property(TARGET Lan_Share APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
endif()