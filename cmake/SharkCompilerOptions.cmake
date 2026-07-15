function(shark_configure_target target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Unknown Shark target: ${target}")
    endif()

    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX>"
        "$<$<COMPILE_LANGUAGE:CXX>:/permissive->"
        "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>"
        "$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    )

    if(SHARK_UCRT_CONTENT_ROOT)
        set_property(TARGET ${target} PROPERTY
            VS_GLOBAL_UCRTContentRoot "${SHARK_UCRT_CONTENT_ROOT}")
    endif()
endfunction()
