/*
 * stub mongoose functions used to remove real code for klocwork scans
 */

#include <sys/socket.h>
#include <arpa/inet.h>
#include "mongoose.h"
#include "common.h"

struct mg_connection;

time_t mg_mgr_poll(struct mg_mgr *mgr, int milli)
{
	UNUSED(mgr);
	UNUSED(milli);

	return (time_t) 0;
}

void mg_mgr_free(struct mg_mgr *mgr)
{
	UNUSED(mgr);
}

void mg_mgr_init(struct mg_mgr *mgr, void *user_data)
{
	UNUSED(mgr);
	UNUSED(user_data);
}

struct mg_connection *mg_bind_opt(struct mg_mgr *mgr, const char *address,
				  mg_event_handler_t handler,
				  struct mg_bind_opts opts)
{
	UNUSED(mgr);
	UNUSED(address);
	UNUSED(handler);
	UNUSED(opts);

	return NULL;
}

void mg_set_protocol_http_websocket(struct mg_connection *nc)
{
	UNUSED(nc);
}

int mg_printf(struct mg_connection *mgr, const char *fmt, ...)
{
	UNUSED(mgr);
	UNUSED(fmt);

	return 0;
}

struct mg_str mg_mk_str_n(const char *s, size_t len)
{
	const struct mg_str mg_static_str = { 0 };
	UNUSED(s);
	UNUSED(len);
	return mg_static_str;
}

int mg_vcmp(const struct mg_str *str2, const char *str1)
{
	UNUSED(str1);
	UNUSED(str2);
	return 0;
}
