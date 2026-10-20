module;

#include <async/dependencies.h>

export module lcf.async;

#define LCF_ASYNC_EXPORT export
#define LCF_ASYNC_HEADER_INLINE
#define LCF_ASYNC_BUILDING_INTERFACE
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 5244)
#endif
#include <async/implementation.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#undef LCF_ASYNC_EXPORT
#undef LCF_ASYNC_HEADER_INLINE
#undef LCF_ASYNC_BUILDING_INTERFACE
