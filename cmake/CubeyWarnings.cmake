function(cubey_configure_warnings target)
    if (NOT CUBEY_ENABLE_WARNINGS)
        return()
    endif()

    if (MSVC)
        target_compile_options(${target} INTERFACE /W4)
        if (CUBEY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} INTERFACE /WX)
        endif()
        return()
    endif()

    target_compile_options(
        ${target}
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
            -Wnull-dereference
    )

    if (CUBEY_WARNINGS_AS_ERRORS)
        target_compile_options(${target} INTERFACE -Werror)
    endif()
endfunction()
