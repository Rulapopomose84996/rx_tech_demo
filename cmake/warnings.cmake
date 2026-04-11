function(rxtech_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)

        if(RXTECH_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name}
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

        if(RXTECH_ENABLE_SANITIZERS)
            if(RXTECH_SANITIZER_KIND STREQUAL "thread")
                set(_rxtech_sanitizer_flags -fsanitize=thread -fno-omit-frame-pointer)
            elseif(RXTECH_SANITIZER_KIND STREQUAL "address")
                set(_rxtech_sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
            else()
                message(FATAL_ERROR "Unsupported RXTECH_SANITIZER_KIND='${RXTECH_SANITIZER_KIND}'. Expected 'address' or 'thread'.")
            endif()

            target_compile_options(${target_name} PRIVATE ${_rxtech_sanitizer_flags})
            target_link_options(${target_name} PRIVATE ${_rxtech_sanitizer_flags})
        endif()

        if(RXTECH_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()
