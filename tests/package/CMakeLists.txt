# Package tests (label `package`): the consumer app in consumer/ configured, built and run (run.cmake) with imlui
# added each way an app can add it (docs/getting-started.md, "Adding imlui to your project"):
#
#   package.subdirectory       add_subdirectory(<imlui source>), with the app's own SDL3 and spdlog (OWN_DEPS)
#   package.fetchcontent       FetchContent_Declare(imlui SOURCE_DIR <imlui source>) + FetchContent_MakeAvailable
#   package.install            cmake --install of this build into package/prefix (the fixture of the two below)
#   package.find_package       find_package(imlui 0.1 CONFIG REQUIRED) on that prefix
#   package.fetchcontent_find  FetchContent_Declare(... FIND_PACKAGE_ARGS 0.1 CONFIG): must take the installed package
#
# Slow the first time (each consumer builds imlui, or rebuilds the installed modules' BMIs: minutes); their build
# folders are kept, so later runs only rebuild what changed. `ctest -L package` runs only these, `ctest -LE package`
# everything else. The consumer runs are labelled `gl` too: the app opens a window, like the smoke tests. Off with
# -DIMLUI_PACKAGE_TESTS=OFF, and not added with a multi-config generator (they pass the parent's CMAKE_BUILD_TYPE on).
# In a sanitizer build only package.subdirectory is added, with IMLUI_SANITIZERS passed on: the consumer sets nothing
# else, so it checks that linking imlui::imlui instruments the app's targets to match.
option(IMLUI_PACKAGE_TESTS "Add the package tests (consumer builds: add_subdirectory, FetchContent, find_package)" ON)
if(NOT IMLUI_PACKAGE_TESTS)
    return()
endif()
get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(multi_config)
    message(STATUS "imlui: no package tests in a multi-config build")
    return()
endif()

set(work ${CMAKE_CURRENT_BINARY_DIR})
function(imlui_add_package_test mode)
    add_test(NAME package.${mode}
             COMMAND ${CMAKE_COMMAND} -DMODE=${mode} -DSOURCE=${PROJECT_SOURCE_DIR} -DWORK=${work}/${mode}
                     -DGENERATOR=${CMAKE_GENERATOR} -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                     -DC_COMPILER=${CMAKE_C_COMPILER} -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                     -DBUILD_TYPE=${CMAKE_BUILD_TYPE} -DMSVC_RUNTIME=${CMAKE_MSVC_RUNTIME_LIBRARY} ${ARGN}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/run.cmake)
    # Each one builds with every core: one at a time.
    set_tests_properties(package.${mode} PROPERTIES LABELS "package;gl" TIMEOUT 1800 RUN_SERIAL ON)
endfunction()

if(IMLUI_SANITIZERS)
    imlui_add_package_test(subdirectory -DSANITIZERS=${IMLUI_SANITIZERS})
    return()
endif()
imlui_add_package_test(subdirectory -DOWN_DEPS=ON)
imlui_add_package_test(fetchcontent)

if(IMLUI_INSTALL)
    set(prefix ${work}/prefix)
    add_test(NAME package.install COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --prefix ${prefix})
    set_tests_properties(package.install PROPERTIES LABELS package FIXTURES_SETUP imlui_package RUN_SERIAL ON)
    foreach(mode find_package fetchcontent_find)
        imlui_add_package_test(${mode} -DPREFIX=${prefix})
        set_tests_properties(package.${mode} PROPERTIES FIXTURES_REQUIRED imlui_package)
    endforeach()
endif()
