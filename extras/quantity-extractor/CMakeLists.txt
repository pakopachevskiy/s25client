# Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf REQUIRED)

set(QUANTITY_EXTRACTOR_PROTOS
    ${PROJECT_SOURCE_DIR}/external/proto-repo/building_locations.proto
    ${PROJECT_SOURCE_DIR}/external/proto-repo/road_locations.proto
    ${PROJECT_SOURCE_DIR}/external/proto-repo/environment_snapshot.proto
    ${PROJECT_SOURCE_DIR}/external/proto-repo/building_quality_snapshot.proto
)
set(PROTOBUF_IMPORT_DIRS ${PROJECT_SOURCE_DIR}/external/proto-repo)
protobuf_generate_cpp(QUANTITY_EXTRACTOR_PROTO_SRCS QUANTITY_EXTRACTOR_PROTO_HDRS ${QUANTITY_EXTRACTOR_PROTOS})
set_source_files_properties(${QUANTITY_EXTRACTOR_PROTO_SRCS} PROPERTIES COMPILE_OPTIONS "-Wno-missing-declarations")

add_library(quantity-extractor-lib STATIC
    QuantityExtractor.h
    QuantityExtractor.cpp
    ProtoWriters.h
    ProtoWriters.cpp
    SavegameLoader.h
    SavegameLoader.cpp
    ${QUANTITY_EXTRACTOR_PROTO_SRCS}
    ${QUANTITY_EXTRACTOR_PROTO_HDRS}
)

target_include_directories(quantity-extractor-lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/libs/s25main
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/s25main
)

target_link_libraries(quantity-extractor-lib
    PUBLIC
        protobuf::libprotobuf
        s25Main
        Boost::filesystem
        Boost::nowide
)

target_compile_features(quantity-extractor-lib PUBLIC cxx_std_17)

add_executable(quantity-extractor main.cpp)
target_link_libraries(quantity-extractor PRIVATE quantity-extractor-lib Boost::filesystem Boost::nowide)

if(WIN32)
    include(GatherDll)
    gather_dll_copy(quantity-extractor)
endif()
