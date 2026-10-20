import lcf.async;

#if defined(LCF_ASYNC_PROBE_FRAME)
auto private_helper = &lcf::async::details::allocate_frame;
#else
using PrivateQueue = lcf::async::details::Queue;
#endif
