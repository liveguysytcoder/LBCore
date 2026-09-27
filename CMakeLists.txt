cmake_minimum_required(VERSION 3.10)
project(Lbcore LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Update Include Directories
# Added subfolders so executer.cpp can find your .h files easily
include_directories(
    .
    LBNet
    LBNet/Packets
    LBNet/Server
    LBBNet
    LBBNet/Packets
    LBBNet/Server
    Logging
    Config
)

# 2. Collect source files
file(GLOB_RECURSE LBNET_SOURCES "LBNet/*.cpp")
file(GLOB_RECURSE LBBNET_SOURCES "LBBNet/*.cpp")
file(GLOB_RECURSE LOGGING_SOURCES "Logging/*.cpp")
file(GLOB_RECURSE CONFIG_SOURCES "Config/*.cpp")

# 3. Create the final source list including the new executer.cpp
set(ALL_SOURCES 
    executer.cpp 
    ${LBNET_SOURCES} 
    ${LBBNET_SOURCES}
    ${LOGGING_SOURCES}
    ${CONFIG_SOURCES}
)

# 4. Create the single executable
add_executable(LBCore ${ALL_SOURCES})

# 5. Link zlib (used by LBBNet/Packets/Compression.cpp)
find_package(ZLIB REQUIRED)
target_link_libraries(LBCore PRIVATE ZLIB::ZLIB)

# 6. Link OpenSSL's crypto lib (EC keygen/ECDH/ECDSA/AES — used by
#    LBBNet/Packets/Encryption.cpp for the login encryption handshake)
find_package(OpenSSL REQUIRED)
target_link_libraries(LBCore PRIVATE OpenSSL::Crypto)

# 7. Link leveldb (used by LBBNet/World/WorldStorage.cpp for world
#    persistence). MUST be Mojang's leveldb-mcpe fork
#    (https://github.com/Mojang/leveldb-mcpe) or a maintained fork of it
#    (e.g. Amulet-Team/leveldb-mcpe, since Mojang's own repo is gone),
#    NOT vanilla Google leveldb -- see WorldStorage.h for why.
#
#    The library NAME differs by fork: Amulet-Team's CMakeLists.txt keeps
#    the CMake target named "leveldb_mcpe" (confirmed: their build output
#    literally says "Built target leveldb_mcpe" and produces
#    libleveldb_mcpe.so), NOT "leveldb" like upstream Mojang's did -- so
#    both names are searched for below, in case you're on a different fork.
#
#    It also isn't installed to a system prefix by default -- a plain
#    `cmake --build .` in the leveldb-mcpe repo just leaves the .so in its
#    own build/ directory and headers in its own include/, it doesn't
#    `make install` anywhere find_path/find_library would look by default.
#    HINTS below check, in order: a vendored copy at
#    Libraries/leveldb-mcpe/ inside this project itself (self-contained,
#    portable -- see the copy commands in project notes), then a sibling
#    ~/leveldb-mcpe checkout (the original Termux layout). If yours lives
#    somewhere else, pass -DCMAKE_PREFIX_PATH=/path/to/leveldb-mcpe when
#    configuring instead of editing this file.
find_path(LEVELDB_INCLUDE_DIR NAMES leveldb/c.h
    HINTS
        ${CMAKE_SOURCE_DIR}/Libraries/leveldb-mcpe/include
        $ENV{HOME}/leveldb-mcpe/include
        ${CMAKE_SOURCE_DIR}/../leveldb-mcpe/include
)
find_library(LEVELDB_LIBRARY NAMES leveldb_mcpe leveldb
    HINTS
        ${CMAKE_SOURCE_DIR}/Libraries/leveldb-mcpe/lib
        $ENV{HOME}/leveldb-mcpe/build
        ${CMAKE_SOURCE_DIR}/../leveldb-mcpe/build
)
if(NOT LEVELDB_INCLUDE_DIR OR NOT LEVELDB_LIBRARY)
    message(FATAL_ERROR
        "leveldb (Mojang's leveldb-mcpe fork, not vanilla Google leveldb) "
        "not found. Build/install it from https://github.com/Mojang/leveldb-mcpe "
        "(or a maintained fork, e.g. Amulet-Team/leveldb-mcpe) before "
        "configuring this project. If it's not at ~/leveldb-mcpe or "
        "Libraries/leveldb-mcpe/, pass "
        "-DCMAKE_PREFIX_PATH=/path/to/leveldb-mcpe when configuring. "
        "See LBBNet/World/WorldStorage.h for details on why the fork "
        "specifically is required.")
endif()
target_include_directories(LBCore PRIVATE ${LEVELDB_INCLUDE_DIR})
target_link_libraries(LBCore PRIVATE ${LEVELDB_LIBRARY})

# leveldb-mcpe is linked as a SHARED library (libleveldb_mcpe.so), not
# static -- that means the LBCore binary needs to find it again at
# RUNTIME, not just at link time. Since it's vendored inside this project
# (or a sibling directory) rather than installed to a standard system lib
# path, the dynamic linker won't find it automatically without help. This
# bakes the .so's directory into the binary itself as an RPATH (relative
# to wherever the binary ends up, via $ORIGIN), so it works right after
# building without needing to `export LD_LIBRARY_PATH=...` every time.
get_filename_component(LEVELDB_LIB_DIR ${LEVELDB_LIBRARY} DIRECTORY)
set_target_properties(LBCore PROPERTIES
    BUILD_RPATH "${LEVELDB_LIB_DIR}"
    INSTALL_RPATH "${LEVELDB_LIB_DIR}"
)
