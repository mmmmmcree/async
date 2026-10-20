#if PROBE_USE_MODULES
import probe.deduction;
#else
#include "deduction.h"
namespace probe { using dependency::k_just; }
#endif

int main()
{
    const auto value = probe::k_just(42);
    return value.m_value != 42;
}
