#pragma once

#include <async/core/protocol.h>
#include <memory_resource>
#include <stop_token>

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
template <sender Source>
auto with_stop_token(Source && source, std::stop_token token)
{ return stde::write_env(std::forward<Source>(source), stde::prop {stde::get_stop_token, token}); }
LCF_ASYNC_EXPORT
template <sender Source>
auto with_allocator(Source && source, std::pmr::memory_resource * resource_p)
{ return stde::write_env(std::forward<Source>(source), stde::prop {stde::get_allocator, std::pmr::polymorphic_allocator<std::byte> {resource_p}}); }

}
