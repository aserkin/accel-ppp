#ifndef TRITON_H
#define TRITON_H

#include <sys/time.h>
#include <stdint.h>

#include "list.h"

struct triton_context_t
{
	const void *tpd; // triton private data, don't touch
	void (*close)(struct triton_context_t*);
	void (*free)(struct triton_context_t*);
	void (*before_switch)(struct triton_context_t *ctx, void *arg);
};

struct triton_md_handler_t
{
	const void *tpd; // triton private data, don't touch!
	int fd;
	int (*read)(struct triton_md_handler_t *);
	int (*write)(struct triton_md_handler_t *);
};

struct triton_timer_t
{
	const void *tpd; // triton private data, don't touch!
	struct timeval expire_tv;
	int period;
	void (*expire)(struct triton_timer_t *);
};

struct triton_sigchld_handler_t
{
	void *tpd;
	int pid;
	void (*handler)(struct triton_sigchld_handler_t *h, int status);
};

struct conf_option_t
{
	struct list_head entry;
	char *name;
	char *val;
};

struct conf_sect_t
{
	const char *name;	
	struct list_head items;
};

struct triton_stat_t
{
	uint32_t mempool_allocated;
	uint32_t mempool_available;
	uint32_t thread_count;
	uint32_t thread_active;
	uint32_t context_count;
	uint32_t context_sleeping;
	uint32_t context_pending;
	uint32_t md_handler_count;
	uint32_t md_handler_pending;
	uint32_t timer_count;
	uint32_t timer_pending;
};

extern struct triton_stat_t triton_stat;
int triton_context_register(struct triton_context_t *, void *arg);
void triton_context_unregister(struct triton_context_t *);
void triton_context_schedule(struct triton_context_t *);
int triton_context_wakeup(struct triton_context_t *);
int triton_context_call(struct triton_context_t *, void (*func)(void *), void *arg);

#define MD_MODE_READ 1
#define MD_MODE_WRITE 2

#define MD_TRIG_EDGE 0
#define MD_TRIG_LEVEL 1

void triton_md_register_handler(struct triton_context_t *, struct triton_md_handler_t *);
void triton_md_unregister_handler(struct triton_md_handler_t *h);
int triton_md_enable_handler(struct triton_md_handler_t *h, int mode);
int triton_md_disable_handler(struct triton_md_handler_t *h,int mode);
void triton_md_set_trig(struct triton_md_handler_t *h, int mode);

int triton_timer_add(struct triton_context_t *ctx, struct triton_timer_t*,int abs_time);
int triton_timer_mod(struct triton_timer_t *,int abs_time);
void triton_timer_del(struct triton_timer_t *);

typedef void (*triton_event_func)(void *);
int triton_event_register_handler(int ev_id, triton_event_func func);
void triton_event_fire(int ev_id, void *arg);

struct conf_sect_t *conf_get_section(const char *name);
char *conf_get_opt(const char *sect, const char *name);

#define TRITON_OK          0
#define TRITON_ERR_NOCOMP -1
#define TRITON_ERR_NOSUPP -2
#define TRITON_ERR_NOINTF -3
#define TRITON_ERR_EXISTS -4
#define TRITON_ERR_NOCHAN -5
#define TRITON_ERR_NOMSG  -6
#define TRITON_ERR_BUSY   -5

int triton_init(const char *conf_file);
int triton_load_modules(const char *md_sect);
void triton_run(void);
void triton_terminate(void);


#define __init __attribute__((constructor))
#define __export __attribute__((visibility("default")))

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#endif