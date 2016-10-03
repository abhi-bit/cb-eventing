CMAKE_MINIMUM_REQUIRED (VERSION 2.8)
IF (${CMAKE_MAJOR_VERSION} GREATER 2)
    CMAKE_POLICY(SET CMP0042 NEW)
ENDIF (${CMAKE_MAJOR_VERSION} GREATER 2)


PROJECT (eventing)

INCLUDE (FindCouchbaseGo)
INCLUDE (FindCouchbaseJemalloc)
INCLUDE (FindCouchbaseEventingDep)

INCLUDE_DIRECTORIES(BEFORE ${REDIS_INCLUDE_DIR} ${LIBCOUCHBASE_INCLUDE_DIR})

INCLUDE_DIRECTORIES(AFTER  ${ICU_INCLUDE_DIR}
                           ${V8_INCLUDE_DIR}
                           ${CURL_INCLUDE_DIR}
                           ${CMAKE_CURRENT_BINARY_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_INSTALL_PREFIX}/include
                           ${Platform_SOURCE_DIR}/include
                           ${rapidjson_SOURCE_DIR}/../
                           ${phosphor_SOURCE_DIR}/include)

SET(EVENTING_SOURCES worker/binding/bucket.cc worker/binding/http_response.cc 
		     worker/binding/n1ql.cc worker/binding/parse_deployment.cc
		     worker/binding/queue.cc worker/binding/worker.cc)

SET(EVENTING_LIBRARIES ${V8_LIBRARIES} ${ICU_LIBRARIES} ${JEMALLOC_LIBRARIES} ${CURL_LIBRARIES} ${REDIS_LIBRARIES} ${LIBCOUCHBASE_LIBRARIES} platform phosphor)
ADD_LIBRARY(v8_binding SHARED ${EVENTING_SOURCES})
SET_TARGET_PROPERTIES(v8_binding PROPERTIES COMPILE_FLAGS "-DCBASSERT_ENABLED=1")
TARGET_LINK_LIBRARIES(v8_binding ${EVENTING_LIBRARIES})
INSTALL(TARGETS v8_binding
        LIBRARY DESTINATION lib)
IF (APPLE)
    SET_TARGET_PROPERTIES(v8_binding
                          PROPERTIES
                          INSTALL_RPATH "@rpath")
ENDIF (APPLE)
#IF (APPLE)
#EXECUTE_PROCESS(
#COMMAND echo "running install_name_tool"
#COMMAND install_name_tool -change @rpath/libv8.dylib ${CMAKE_INSTALL_PREFIX}/lib/libv8.dylib ${CMAKE_CURRENT_BINARY_DIR}/libv8_binding
#COMMAND install_name_tool -change @loader_path/libv8.dylib ${CMAKE_INSTALL_PREFIX}/lib/libv8.dylib ${CMAKE_CURRENT_BINARY_DIR}/libv8_binding
#COMMAND install_name_tool -change @rpath/libjemalloc.2.dylib ${CMAKE_INSTALL_PREFIX}/lib/libjemalloc.2.dylib ${CMAKE_CURRENT_BINARY_DIR}/libv8_binding
#COMMAND install_name_tool -change @rpath/libcouchbase.2.dylib /usr/local/lib/libcouchbase.2.dylib ${CMAKE_CURRENT_BINARY_DIR}/libv8_binding
#COMMAND install_name_tool -change libhiredis.0.13.dylib /usr/local/lib/libhiredis.dylib ${CMAKE_CURRENT_BINARY_DIR}/libv8_binding
#)
#ENDIF(APPLE)

#SET (_ldflags "-L${CMAKE_CURRENT_BINARY_DIR} -lv8_binding")
SET (_ldflags "")
IF (APPLE)
  # On OS X 10.11 (El Capitan) upwards we can no longer use DYLD_LIBRARY_PATH to locate
  # runtime dependancies. Instead add the location of forestdb etc to the actual
  # binary itself.
  SET (_ldflags "${_ldflags} -extldflags '-Wl,-rpath,@executable_path/../lib'")
ENDIF ()

SET (_eventing_ldflags "${_ldflags}")
IF (WIN32)
  INCLUDE (PlatformIntrospection)
  _DETERMINE_ARCH (_arch)
  IF (NOT _arch STREQUAL x86)
    SET (_eventing_ldflags "${_eventing_ldflags} -linkmode internal")
  ENDIF ()
ENDIF ()
GoInstall (TARGET go_eventing PACKAGE github.com/abhi-bit/eventing/go_eventing
  GOPATH "${PROJECT_SOURCE_DIR}/../../../.." "${GODEPSDIR}"
  DEPENDS v8_binding
  CGO_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/worker"
  CGO_LIBRARY_DIRS "${CMAKE_CURRENT_BINARY_DIR} -lv8_binding"
  LDFLAGS "${_eventing_ldflags}"
  INSTALL_PATH bin OUTPUT go_eventing
  GOVERSION 1.6)
IF (APPLE)
    SET_TARGET_PROPERTIES(go_eventing
                          PROPERTIES
                          INSTALL_RPATH "@rpath")
ENDIF (APPLE)

#IF (APPLE)
#EXECUTE_PROCESS(
#COMMAND install_name_tool -change libv8_binding.dylib ${CMAKE_INSTALL_PREFIX}/lib/libv8_binding ${CMAKE_INSTALL_PREFIX}/bin/go_eventing
#)
#ENDIF (APPLE)

