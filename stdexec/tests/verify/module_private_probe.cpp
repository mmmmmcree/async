import lcf.async;

#if defined(LCF_ASYNC_PROBE_FRAME)
auto private_helper = &lcf::async::details::allocate_frame;
#elif defined(LCF_ASYNC_PROBE_QUEUE)
using PrivateQueue = lcf::async::details::Queue;
#else
auto private_helper = &lcf::async::details::allocate_frame;
#endif
