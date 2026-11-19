cmake_minimum_required(VERSION 3.16)

project(RegFileViewer
    VERSION 1.0.0
    DESCRIPTION "中登开放式基金交换协议 / 理财登 接口数据文件阅读器"
    LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(MSVC)
    add_compile_options(/utf-8 /permissive-)
    # 目标运行环境：Windows 7 SP1 及以上
    add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)

    # Qt 5.15 的 QT_MAKE_CHECKED_ARRAY_ITERATOR 依赖 stdext，而 VS2022 新版
    # MSVC STL 已移除该命名空间。这里检测当前工具集，必要时强制包含兼容补丁。
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #include <iterator>
        #include <cstddef>
        int main() {
            int data[2] = { 1, 2 };
            auto it = stdext::make_checked_array_iterator(data, 2);
            (void)it;
            return 0;
        }" RFV_HAVE_STDEXT_ARRAY_ITERATOR)

    if(NOT RFV_HAVE_STDEXT_ARRAY_ITERATOR)
        message(STATUS "当前 MSVC 已移除 stdext 检查迭代器，启用 Qt 5.15 兼容补丁")
        add_compile_options(
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/compat/msvc_stdext_shim.h")
    endif()
endif()

# Qt 5.15.2 为 Windows 7 的最后一个官方支持版本
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

set(RFV_CORE_SOURCES
    src/core/encoding.cpp
    src/core/inifile.cpp
    src/core/interface.cpp
    src/core/configrepository.cpp
    src/core/fileparser.cpp
    src/core/tablemodel.cpp
    src/core/exporter.cpp
    src/core/encoding.h
    src/core/inifile.h
    src/core/interface.h
    src/core/configrepository.h
    src/core/fileparser.h
    src/core/tablemodel.h
    src/core/exporter.h
)

add_library(rfv_core STATIC ${RFV_CORE_SOURCES})
target_include_directories(rfv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rfv_core PUBLIC Qt5::Core Qt5::Gui)
target_compile_definitions(rfv_core PUBLIC
    RFV_HAVE_TEXTCODEC
    RFV_SOURCE_CONF="${CMAKE_CURRENT_SOURCE_DIR}/conf")

# Windows 可执行文件图标
set(RFV_WIN_RC "")
if(WIN32)
    set(RFV_WIN_RC "${CMAKE_CURRENT_SOURCE_DIR}/res/app.rc")
    set_source_files_properties(${RFV_WIN_RC} PROPERTIES
        COMPILE_FLAGS "/I${CMAKE_CURRENT_SOURCE_DIR}/res")
endif()

add_executable(RegFileViewer WIN32
    src/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/choosedialog.cpp
    src/gui/choosedialog.h
    src/gui/filetyperefdialog.cpp
    src/gui/filetyperefdialog.h
    src/gui/infopanel.cpp
    src/gui/infopanel.h
    ${RFV_WIN_RC}
)
target_link_libraries(RegFileViewer PRIVATE rfv_core Qt5::Widgets)
target_compile_definitions(RegFileViewer PRIVATE
    RFV_PROJECT_VERSION="${PROJECT_VERSION}"
)

# 控制台自检程序，用于在没有界面的环境下验证解析结果
add_executable(rfv_selftest tests/selftest.cpp)
target_link_libraries(rfv_selftest PRIVATE rfv_core)

# 构建完成后把 conf 目录复制到可执行文件旁边
add_custom_command(TARGET RegFileViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/conf"
            "$<TARGET_FILE_DIR:RegFileViewer>/conf"
    COMMENT "复制 conf 解析配置")

if(WIN32)
    get_target_property(_qt_qmake Qt5::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    find_program(RFV_WINDEPLOYQT windeployqt HINTS "${_qt_bin_dir}")
    if(RFV_WINDEPLOYQT)
        add_custom_command(TARGET RegFileViewer POST_BUILD
            COMMAND "${RFV_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                    "$<TARGET_FILE:RegFileViewer>"
            COMMENT "windeployqt (Qt ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR})")
    endif()
endif()
