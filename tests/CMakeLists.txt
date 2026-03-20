add_executable(mycoroutine_smoke_test smoke/main.cpp)
target_link_libraries(mycoroutine_smoke_test PRIVATE mycoroutine::mycoroutine)
add_test(NAME mycoroutine_smoke_test COMMAND mycoroutine_smoke_test)

add_executable(mycoroutine_shared_stack_test shared_stack/main.cpp)
target_link_libraries(mycoroutine_shared_stack_test PRIVATE mycoroutine::mycoroutine)
add_test(NAME mycoroutine_shared_stack_test COMMAND mycoroutine_shared_stack_test)

add_executable(mycoroutine_nested_test nested/main.cpp)
target_link_libraries(mycoroutine_nested_test PRIVATE mycoroutine::mycoroutine)
add_test(NAME mycoroutine_nested_test COMMAND mycoroutine_nested_test)

add_executable(mycoroutine_policy_test policy/main.cpp)
target_link_libraries(mycoroutine_policy_test PRIVATE mycoroutine::mycoroutine)
add_test(NAME mycoroutine_policy_test COMMAND mycoroutine_policy_test)

add_executable(mycoroutine_pool_test pool/main.cpp)
target_link_libraries(mycoroutine_pool_test PRIVATE mycoroutine::mycoroutine)
add_test(NAME mycoroutine_pool_test COMMAND mycoroutine_pool_test)

add_executable(mycoroutine_hook_test hook/main.cpp)
target_link_libraries(mycoroutine_hook_test PRIVATE mycoroutine::mycoroutine)
add_test(NAME mycoroutine_hook_test COMMAND mycoroutine_hook_test)

if(MYCOROUTINE_BUILD_BENCHMARKS)
    add_executable(mycoroutine_benchmark bench/main.cpp)
    target_link_libraries(mycoroutine_benchmark PRIVATE mycoroutine::mycoroutine Threads::Threads)
endif()

if(MYCOROUTINE_BUILD_NETWORK_DEMOS)
    add_executable(epoll_http_demo epoll/main.cpp)

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBEVENT QUIET libevent)
    endif()

    if(LIBEVENT_FOUND)
        add_executable(libevent_http_demo libevent/main.cpp)
        target_include_directories(libevent_http_demo PRIVATE ${LIBEVENT_INCLUDE_DIRS})
        target_link_libraries(libevent_http_demo PRIVATE ${LIBEVENT_LIBRARIES})
    else()
        message(STATUS "libevent not found, skip libevent_http_demo target")
    endif()
endif()
