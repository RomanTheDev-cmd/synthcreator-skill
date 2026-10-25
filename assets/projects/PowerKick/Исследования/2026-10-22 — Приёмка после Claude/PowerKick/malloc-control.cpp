#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
extern "C" { using Logger=void(uint32_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uint32_t); extern Logger* malloc_logger; }
std::atomic<bool> tracking{false}; std::atomic<int> count{0};
void logger(uint32_t t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uint32_t) { if(tracking.load(std::memory_order_relaxed)&&(t&2u)) count.fetch_add(1,std::memory_order_relaxed); }
int main(){ auto* old=malloc_logger;malloc_logger=logger;tracking=true;void* p=std::malloc(32768); tracking=false;int n=count.load();std::printf("malloc=%p count=%d\n",p,n);std::free(p);malloc_logger=old;return n>0?0:1;}
