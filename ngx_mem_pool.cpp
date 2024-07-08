// ngx_mem_pool.cpp
#include "ngx_mem_pool.h"
#include <stdlib.h>

void* ngx_mem_pool::ngx_create_pool(size_t size)
{
	ngx_pool_s* p;
	p = (ngx_pool_s*)malloc(size);
	if (p == nullptr)
	{
		return nullptr;
	}

	p->d.last = (u_char*)p + sizeof(ngx_pool_s);
	p->d.end = (u_char*)p + size;
	p->d.next = nullptr;
	p->d.failed = 0;

	size = size - sizeof(ngx_pool_s); // 内存池实际的可分配内存空间
	p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

	p->current = p;
	p->large = nullptr;
	p->cleanup = nullptr;
	pool = p;

	return p;
}
//分配新的小块内存池
inline void* ngx_mem_pool::ngx_palloc_small(size_t size, ngx_uint_t align)
{
	u_char* m;
	ngx_pool_s* p;

	p = pool->current;

	do {
		m = (u_char*)p->d.last;
		// m = (u_char*)p->current;

		if (align)
		{
			m = ngx_align_ptr(m, NGX_ALIGNMENT);
		}

		if ((size_t)(p->d.end - m) >= size)
		{
			p->d.last = m + size;
			return m;
		}

		p = p->d.next; // 到下一个小块内存池尝试
	} while (p);

	return ngx_palloc_block(size);
}

void* ngx_mem_pool::ngx_palloc_block(size_t size)
{
	u_char* m;
	size_t       psize;
	ngx_pool_s* p, * newpool;

	psize = (size_t)(pool->d.end - (u_char*)pool);

	m = (u_char*)malloc(psize);
	if (m == NULL) {
		return NULL;
	}

	newpool = (ngx_pool_s*)m;

	newpool->d.end = m + psize;
	newpool->d.next = NULL;
	newpool->d.failed = 0;

	m += sizeof(ngx_pool_data_t);
	m = ngx_align_ptr(m, NGX_ALIGNMENT);
	newpool->d.last = m + size;

	for (p = pool->current; p->d.next; p = p->d.next) {
		if (p->d.failed++ > 4) { // 分配内存失败次数大于4就将current设置为下一个内存池块
			pool->current = p->d.next;
		}
	}

	p->d.next = newpool;

	return m;
}
//分配大块内存
void* ngx_mem_pool::ngx_palloc_large(size_t size)
{
	void* p;
	ngx_uint_t         n;
	ngx_pool_large_s* large;

	p = malloc(size); // malloc,实际分配的内存空间
	if (p == nullptr) {
		return nullptr;
	}

	n = 0;
	// 复用已释放空间内存头信息
	for (large = pool->large; large; large = large->next) {
		if (large->alloc == nullptr) {
			large->alloc = p;
			return p;
		}

		if (n++ > 3) {
			break;
		}
	}
	// 大块内存的头信息也是存在小块内存池中！！
	large = (ngx_pool_large_s*)ngx_palloc_small(sizeof(ngx_pool_large_s), 1);
	if (large == nullptr) {
		free(p);
		return nullptr;
	}

	large->alloc = p;
	large->next = pool->large; // 头插法
	pool->large = large;

	return p;
}

void ngx_mem_pool::ngx_pfree(void* p)
{
	ngx_pool_large_s* l;
	// 查找 alloc 与 p相同的内存将其释放掉
	for (l = pool->large; l; l = l->next)
	{
		if (p == l->alloc)
		{
			free(l->alloc);
			l->alloc = nullptr;
			return;
		}
	}
}
void* ngx_mem_pool::ngx_palloc(size_t size)
{
	if (size <= pool->max)
	{
		return ngx_palloc_small(size, 1);
	}
	return ngx_palloc_large(size);
}
void* ngx_mem_pool::ngx_pnalloc(size_t size)
{
	if (size <= pool->max) {
		return ngx_palloc_small(size, 0); // 不对齐
	}
	return ngx_palloc_large(size);
}

void* ngx_mem_pool::ngx_pcalloc(size_t size)
{
	void* p;
	p = ngx_palloc(size);
	if (p)
	{
		ngx_memzero(p, size);
	}
	return p;
}

void ngx_mem_pool::ngx_reset_pool()
{
	ngx_pool_s* p;
	ngx_pool_large_s* l;

	for (l = pool->large; l; l = l->next)
	{
		if (l->alloc)
		{
			free(l->alloc);
		}
	}
	
	// 特殊处理第一块内存池
	p = pool;
	p->d.last = (u_char*)p + sizeof(ngx_pool_s);
	p->d.failed = 0;

	// 第二个到结尾内存池没有多余的头部信息，如current、cleanup
	for (p = p->d.next; p; p = p->d.next) {
		p->d.last = (u_char*)p + sizeof(ngx_pool_data_t);
		p->d.failed = 0;
	}

	pool->current = pool;
	pool->large = nullptr;
}

void ngx_mem_pool::ngx_destroy_pool()
{
	ngx_pool_s* p, * n;
	ngx_pool_large_s* l;
	ngx_pool_cleanup_s* c;

	// 释放外部资源，通过调用自己实现的释放资源函数
	for (c = pool->cleanup; c; c = c->next)
	{
		if (c->handler)
		{
			c->handler(c->data);
		}
	}

	// 大块内存池释放
	for (l = pool->large; l; l = l->next)
	{
		if (l->alloc)
		{
			free(l->alloc);
		}
	}
	// 小块内存池释放
	for (p = pool, n = pool->d.next; ; p = n, n = n->d.next)
	{
		free(p);
		if (n == nullptr)
		{
			break;
		}
	}

}
// size表示清理函数参数的大小
ngx_pool_cleanup_s* ngx_mem_pool::ngx_pool_cleanup_add(size_t size)
{
	ngx_pool_cleanup_s* c;

	// 头部信息存储在小块内存池
	c = (ngx_pool_cleanup_s*)ngx_palloc(sizeof(ngx_pool_cleanup_s));
	if (c == nullptr)
	{
		return nullptr;
	}

	if (size)
	{
		c->data = ngx_palloc(size);
		if (c->data == nullptr)
		{
			return nullptr;
		}
	}
	else
	{
		c->data = nullptr;
	}

	// 头插
	c->handler = nullptr;
	c->next = pool->cleanup; 

	pool->cleanup = c;

	return c;
}

