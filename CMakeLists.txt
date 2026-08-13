cmake_minimum_required(VERSION 3.21)

project(LMDBArchiver VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(FetchContent)
find_package(Threads REQUIRED)
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

FetchContent_Declare(
    lmdb
    GIT_REPOSITORY https://github.com/LMDB/lmdb.git
    GIT_TAG LMDB_0.9.33
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR libraries/liblmdb
)
FetchContent_GetProperties(lmdb)
if(NOT lmdb_POPULATED)
    FetchContent_Populate(lmdb)
endif()

add_library(lmdb_vendor STATIC
    ${lmdb_SOURCE_DIR}/libraries/liblmdb/mdb.c
    ${lmdb_SOURCE_DIR}/libraries/liblmdb/midl.c
)
target_include_directories(lmdb_vendor PUBLIC ${lmdb_SOURCE_DIR}/libraries/liblmdb)
target_link_libraries(lmdb_vendor PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_compile_definitions(lmdb_vendor PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(lmdb_vendor PRIVATE Advapi32)
endif()

# miniz (2.2.0, public domain) — vendored as static sources for standard gzip (RFC 1952).
# We only need deflate/inflate + crc32, so the ZIP archive and stdio APIs are disabled.
add_library(miniz_vendor STATIC
    vendor/miniz/miniz.c
    vendor/miniz/miniz_tdef.c
    vendor/miniz/miniz_tinfl.c
)
target_include_directories(miniz_vendor PUBLIC vendor/miniz)
target_compile_definitions(miniz_vendor PRIVATE MINIZ_NO_STDIO MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ARCHIVE_WRITING_APIS)
if(MSVC)
    target_compile_definitions(miniz_vendor PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(miniz_vendor PRIVATE /W1)
endif()

add_library(lmdbarchiver_core STATIC
    src/archive/archive.cpp
    src/archive/archive.h
    src/archive/archiveentry.h
)
target_include_directories(lmdbarchiver_core PUBLIC src)
target_link_libraries(lmdbarchiver_core PUBLIC Qt${QT_VERSION_MAJOR}::Core lmdb_vendor miniz_vendor)

add_executable(LMDBArchiver WIN32
    src/main.cpp
    src/app/app.cpp
    src/app/app.h
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/archivemodel.cpp
    src/app/archivemodel.h
    src/app/dropview.cpp
    src/app/dropview.h
    src/platform/shellintegration.cpp
    src/platform/shellintegration.h
    resources/resources.qrc
)
if(WIN32)
    target_sources(LMDBArchiver PRIVATE resources/windows.rc)
endif()
target_link_libraries(LMDBArchiver PRIVATE lmdbarchiver_core Qt${QT_VERSION_MAJOR}::Widgets)
if(WIN32)
    target_link_libraries(LMDBArchiver PRIVATE Advapi32 Shell32)
endif()

add_executable(LMDBArchiverCLI src/cli/main.cpp)
target_link_libraries(LMDBArchiverCLI PRIVATE lmdbarchiver_core Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(LMDBArchiverCLI PRIVATE LMDBARCHIVER_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(LMDBArchiverCLI PRIVATE /W4 /permissive- /utf-8)
endif()
target_compile_definitions(LMDBArchiver PRIVATE
    LMDBARCHIVER_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(MSVC)
    target_compile_options(lmdbarchiver_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(LMDBArchiver PRIVATE /W4 /permissive- /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS LMDBArchiver LMDBArchiverCLI RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md LICENSE DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(lmdbarchiver_tests tests/archive_test.cpp)
    target_link_libraries(lmdbarchiver_tests PRIVATE lmdbarchiver_core Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME archive-core COMMAND lmdbarchiver_tests)
endif()

set(CPACK_PACKAGE_NAME "LMDB Archiver")
set(CPACK_PACKAGE_VENDOR "embistel")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
include(CPack)
