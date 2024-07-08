// ngx_mem_pool.h
#pragma once
#include <string.h>
#include<memory.h>
using u_char = unsigned char;
using ngx_uint_t = unsigned int;

struct ngx_pool_s;
//buf缓存区清零
#define ngx_memzero(buf, n)       (void) memset(buf, 0, n)
//小块内存分配考虑字节对齐时的单位
#define NGX_ALIGNMENT   sizeof(unsigned long)
//把树值d调整到临近的a的倍数
#define ngx_align(d, a)     (((d) + (a - 1)) & ~(a - 1))
//把指针p调整到a的临近的倍数
#define ngx_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))


// 移植 nginx内存池代码，用C++ oop来实现
// _s代表struct, _t代表typedef之后的类型

// 清理资源的类型，每个对象绑定一个函数
typedef void (*ngx_pool_cleanup_pt)(void* data);
struct ngx_pool_cleanup_s {
	ngx_pool_cleanup_pt   handler; // 自定义的资源释放函数
	void* data; // handler的参数
	ngx_pool_cleanup_s* next; // 指向下一个清理操作（对象）
};

// 大块内存的头部信息
struct ngx_pool_large_s {
	ngx_pool_large_s* next; // 大块内存的头部信息用链表连接起来
	void* alloc; // 指向实际分配出的内存
};

// 小块内存的内存池的头部字段
struct ngx_pool_data_t {
	u_char* last;
	u_char* end;
	ngx_pool_s* next;
	ngx_uint_t            failed;
};

struct ngx_pool_s {
	ngx_pool_data_t       d; // 小块内存池中的相关指针及参数
	size_t                max;
	ngx_pool_s* current;

	ngx_pool_large_s* large; // 大块内存分配的起点
	ngx_pool_cleanup_s* cleanup;
};

// 从池中可获得的最大内存数为4095，即一个页面的大小
const int ngx_pagesize = 4096;
// 小块内存池可分配的最大空间
const int NGX_MAX_ALLOC_FROM_POOL = ngx_pagesize - 1;
// 表示一个默认内存池开辟的大小
const int NGX_DEFAULT_POOL_SIZE = 16 * 1024; // 16K

// 按16B对齐
const int NGX_POOL_ALIGNMENT = 16;
// ngx_align就是和SGI STL二级空间配置器的 _S_round_up函数相同，对齐到NGX_POOL_ALIGNMENT
// ngx小块内存池的最小size调整为 NGX_POOL_ALIGNMENT 的倍数
const int NGX_MIN_POOL_SIZE = ngx_align((sizeof(ngx_pool_s) + 2 * sizeof(ngx_pool_large_s)), NGX_POOL_ALIGNMENT);

class ngx_mem_pool
{
public:
	void* ngx_create_pool(size_t size);
	// 内存分配，支持内存对齐
	void* ngx_palloc(size_t size); 
	// 内存分配，不支持内存对齐
	void* ngx_pnalloc(size_t size); 
	// 内存分配，支持内存初始化为0
	void* ngx_pcalloc(size_t size); 
	// 大块内存释放
	void ngx_pfree(void* p); 
	// 内存池重置函数
	void ngx_reset_pool();
	// 内存池销毁函数
	void ngx_destroy_pool(); 
	// 内存池清理操作添加函数
	ngx_pool_cleanup_s* ngx_pool_cleanup_add(size_t size);
	
		
private:
	ngx_pool_s* pool; // 管理内存池的指针
	// 小块内存分配
	inline void* ngx_palloc_small(size_t size, ngx_uint_t align);
	// 大块内存分配
	void* ngx_palloc_large(size_t size);
	// 分配的新的小块内存池
	void* ngx_palloc_block(size_t size);
};

