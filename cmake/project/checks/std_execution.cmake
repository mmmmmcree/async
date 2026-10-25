# Probe the actual compiler, standard library and language mode.
function(async_check_std_execution output)
    include(CheckCXXSourceCompiles)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    unset(async_has_std_execution CACHE)
    check_cxx_source_compiles([=[
        #include <execution>
        #include <tuple>
        #include <utility>
        namespace ex = std::execution;
        static_assert(ex::sender<decltype(ex::just(1))>);
        int main() {
            auto work = ex::just(21) | ex::then([](int n) { return n * 2; });
            auto result = std::this_thread::sync_wait(std::move(work));
            return std::get<0>(*result) == 42 ? 0 : 1;
        }
    ]=] async_has_std_execution)
    set(${output} "${async_has_std_execution}" PARENT_SCOPE)
endfunction()
