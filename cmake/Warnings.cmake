function(catro_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
    else()
        # Domain records give every member a default initializer and are built with partial
        # designated initializers, which -Wmissing-field-initializers would reject.
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror
                                                 -Wno-missing-field-initializers)
    endif()
endfunction()
