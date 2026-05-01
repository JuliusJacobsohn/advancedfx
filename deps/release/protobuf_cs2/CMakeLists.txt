cmake_minimum_required(VERSION 3.25)

project ("protobuf_cs2")

# CS2 still uses the Source 2 SDK / SourceMod-family protobuf ABI around 3.21.
# AfxHookSource2 needs this only to parse CS2-owned user-message objects.
include(FetchContent)
set(protobuf_INSTALL OFF)
set(protobuf_BUILD_TESTS OFF)
set(protobuf_BUILD_CONFORMANCE OFF)
set(protobuf_BUILD_EXAMPLES OFF)
set(protobuf_BUILD_PROTOBUF_BINARIES OFF)
set(protobuf_BUILD_PROTOC_BINARIES OFF)
set(protobuf_BUILD_SHARED_LIBS OFF)
set(protobuf_MSVC_STATIC_RUNTIME OFF)
FetchContent_Declare(
	protobuf_cs2
	GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
	GIT_TAG v3.21.8
)
FetchContent_MakeAvailable(protobuf_cs2)

if(TARGET libprotobuf-lite AND NOT TARGET protobuf::libprotobuf-lite)
	add_library(protobuf::libprotobuf-lite ALIAS libprotobuf-lite)
endif()
