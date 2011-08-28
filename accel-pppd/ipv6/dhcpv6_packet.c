#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "log.h"
#include "memdebug.h"

#include "dhcpv6.h"

#define BUF_SIZE 4096

struct dict_option {
	int code;
	const char *name;
	int recv;
	int len;
	void (*print)(struct dhcpv6_option *, void (*)(const char *fmt, ...));
};

static void print_clientid(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_ia_na(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_ia_ta(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_ia_addr(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_oro(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_uint8(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_time(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_ipv6addr(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_ipv6addr_array(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_status(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_uint64(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_reconf(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_dnssl(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));
static void print_ia_prefix(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...));

static struct dict_option known_options[] = {
	{ D6_OPTION_CLIENTID, "Client-ID", 1, 0, print_clientid },
	{ D6_OPTION_SERVERID, "Server-ID", 0, 0, print_clientid },
	{ D6_OPTION_IA_NA, "IA-NA", 1, sizeof(struct dhcpv6_opt_ia_na), print_ia_na },
	{ D6_OPTION_IA_TA, "IA-TA", 1, sizeof(struct dhcpv6_opt_ia_ta), print_ia_ta },
	{ D6_OPTION_IAADDR, "IA-Addr", 1, sizeof(struct dhcpv6_opt_ia_addr), print_ia_addr },
	{ D6_OPTION_ORO, "Option-Request", 1, 0, print_oro },
	{ D6_OPTION_PREFERENCE, "Preference", 0, 0, print_uint8 },
	{ D6_OPTION_ELAPSED_TIME, "Elapsed-Time", 1, 0, print_time },
	{ D6_OPTION_RELAY_MSG, "Relay-Message", 1, 0 },
	{ D6_OPTION_AUTH, "Auth", 1, 0 },
	{ D6_OPTION_PREFERENCE, "Server-Unicast", 0, 0, print_ipv6addr },
	{ D6_OPTION_STATUS_CODE, "Status", 0, 0, print_status },
	{ D6_OPTION_RAPID_COMMIT, "Rapid-Commit", 1, 0 },
	{ D6_OPTION_USER_CLASS, "User-Class", 1, 0 },
	{ D6_OPTION_VENDOR_CLASS, "Vendor-Class", 1, 0, print_uint64 },
	{ D6_OPTION_VENDOR_SPECIFIC, "Vendor-Specific", 1, 0, print_uint64 },
	{ D6_OPTION_INTERFACE_ID, "Interface-ID", 1, 0, print_uint64 },
	{ D6_OPTION_RECONF_MSG, "Reconfigure", 0, 0, print_reconf },
	{ D6_OPTION_RECONF_ACCEPT, "Reconfigure-Accept", 1, 0 },
	{ D6_OPTION_DNS_SERVERS, "DNS", 1, 0, print_ipv6addr_array },
	{ D6_OPTION_DOMAIN_LIST, "DNSSL", 1, 0, print_dnssl },
	{ D6_OPTION_IA_PD, "IA-PD", 1, sizeof(struct dhcpv6_opt_ia_na), print_ia_na },
	{ D6_OPTION_IAPREFIX, "IA-Prefix", 1, sizeof(struct dhcpv6_opt_ia_prefix), print_ia_prefix },
	{ 0 }
};

static void *parse_option(void *ptr, void *endptr, struct list_head *opt_list)
{
	struct dict_option *dopt;
	struct dhcpv6_opt_hdr *opth = ptr;
	struct dhcpv6_option *opt;

	if (ptr + sizeof(*opth) + ntohs(opth->len) > endptr) {
		log_warn("dhcpv6: invalid packet received\n");
		return NULL;
	}
	
	opt = _malloc(sizeof(*opt));
	if (!opt) {
		log_emerg("out of memory\n");
		return NULL;
	}

	memset(opt, 0, sizeof(*opt));
	INIT_LIST_HEAD(&opt->opt_list);
	opt->hdr = ptr;
	list_add_tail(&opt->entry, opt_list);

	for (dopt = known_options; dopt->code; dopt++) {
		if (dopt->code)
			break;
	}
	
	if (dopt->len) {
		endptr = ptr + sizeof(*opth) + ntohs(opth->len);
		ptr += sizeof(*opth) + dopt->len;
		while (ptr < endptr) {
			ptr = parse_option(ptr, endptr, &opt->opt_list);
			if (!ptr)
				return NULL;
		}
	} else
		ptr = ptr + sizeof(*opth) + ntohs(opth->len);
	
	return ptr;
}

struct dhcpv6_packet *dhcpv6_packet_parse(const void *buf, size_t size)
{
	struct dhcpv6_packet *pkt;
	struct dhcpv6_opt_hdr *opth;
	void *ptr, *endptr;

	pkt = _malloc(sizeof(*pkt));
	if (!pkt) {
		log_emerg("out of memory\n");
		return NULL;
	}

	memset(pkt, 0, sizeof(*pkt));
	INIT_LIST_HEAD(&pkt->opt_list);

	pkt->hdr = _malloc(size);
	if (!pkt->hdr) {
		log_emerg("out of memory\n");
		_free(pkt);
		return NULL;
	}

	memcpy(pkt->hdr, buf, size);

	ptr = pkt->hdr->data;
	endptr =  ((void *)pkt->hdr) + size;

	while (ptr < endptr) {
		opth = ptr;
		if (opth->code == htons(D6_OPTION_CLIENTID))
			pkt->clientid = ptr;
		else if (opth->code == htons(D6_OPTION_SERVERID))
			pkt->serverid = ptr;
		ptr = parse_option(ptr, endptr, &pkt->opt_list);
		if (!ptr) {
			dhcpv6_packet_free(pkt);
			return NULL;
		}
	}

	return pkt;
}

struct dhcpv6_option *dhcpv6_option_alloc(struct dhcpv6_packet *pkt, int code, int len)
{
	struct dhcpv6_option *opt;
	
	opt = _malloc(sizeof(*opt));
	if (!opt) {
		log_emerg("out of memory\n");
		return NULL;
	}
	
	memset(opt, 0, sizeof(*opt));
	INIT_LIST_HEAD(&opt->opt_list);

	opt->hdr = pkt->endptr;
	opt->hdr->code = htons(code);
	opt->hdr->len = htons(len);

	pkt->endptr += sizeof(struct dhcpv6_opt_hdr) + len;

	list_add_tail(&opt->entry, &pkt->opt_list);

	return opt;
}

struct dhcpv6_option *dhcpv6_nested_option_alloc(struct dhcpv6_packet *pkt, struct dhcpv6_option *popt, int code, int len)
{
	struct dhcpv6_option *opt;
	
	opt = _malloc(sizeof(*opt));
	if (!opt) {
		log_emerg("out of memory\n");
		return NULL;
	}
	
	memset(opt, 0, sizeof(*opt));
	INIT_LIST_HEAD(&opt->opt_list);
	opt->parent = popt;

	opt->hdr = pkt->endptr;
	opt->hdr->code = htons(code);
	opt->hdr->len = htons(len);

	list_add_tail(&opt->entry, &popt->opt_list);

	pkt->endptr += sizeof(struct dhcpv6_opt_hdr) + len;

	while (popt) {
		popt->hdr->len = htons(ntohs(popt->hdr->len) + sizeof(struct dhcpv6_opt_hdr) + len);
		popt = popt->parent;
	}

	return opt;
}


struct dhcpv6_packet *dhcpv6_packet_alloc_reply(struct dhcpv6_packet *req, int type)
{
	struct dhcpv6_packet *pkt = _malloc(sizeof(*pkt));
	struct dhcpv6_option *opt;
	
	if (!pkt) {
		log_emerg("out of memory\n");
		return NULL;
	}

	memset(pkt, 0, sizeof(*pkt));
	INIT_LIST_HEAD(&pkt->opt_list);
	pkt->ppp = req->ppp;

	pkt->hdr = _malloc(BUF_SIZE);
	if (!pkt->hdr) {
		log_emerg("out of memory\n");
		_free(pkt);
		return NULL;
	}

	pkt->hdr->type = type;
	pkt->hdr->trans_id = req->hdr->trans_id;

	pkt->endptr = pkt->hdr->data;

	opt = dhcpv6_option_alloc(pkt, D6_OPTION_SERVERID, ntohs(req->serverid->hdr.len));
	memcpy(opt->hdr, req->serverid, sizeof(struct dhcpv6_opt_hdr) + ntohs(req->serverid->hdr.len));

	opt = dhcpv6_option_alloc(pkt, D6_OPTION_CLIENTID, ntohs(req->clientid->hdr.len));
	memcpy(opt->hdr, req->clientid, sizeof(struct dhcpv6_opt_hdr) + ntohs(req->clientid->hdr.len));

	return pkt;
}

static void free_options(struct list_head *opt_list)
{
	struct dhcpv6_option *opt;

	while (!list_empty(opt_list)) {
		opt = list_entry(opt_list->next, typeof(*opt), entry);
		list_del(&opt->entry);
		free_options(&opt->opt_list);
		_free(opt);
	}
}

void dhcpv6_packet_free(struct dhcpv6_packet *pkt)
{
	free_options(&pkt->opt_list);
	_free(pkt->hdr);
	_free(pkt);
}

static void print_options(struct list_head *opt_list, int level, void (*print)(const char *fmt, ...))
{
	struct dhcpv6_option *opt;
	struct dict_option *dopt;
	const char l_open[] = {'<', '{', '('};
	const char l_close[] = {'>', '}', ')'};

	if (level >= sizeof(l_open))
		level = sizeof(l_open) - 1;

	list_for_each_entry(opt, opt_list, entry) {
		for (dopt = known_options; dopt->code; dopt++) {
			if (htons(dopt->code) == opt->hdr->code)
				break;
		}
		if (dopt->code) {
			print(" %c%s", l_open[level], dopt->name);
			if (dopt->print)
				dopt->print(opt, print);

			print_options(&opt->opt_list, level + 1, print);

			print("%c", l_close[level]);
		} else
			print(" %cOption %i%c", l_open[level], ntohs(opt->hdr->code), l_close[level]);
	}
}

void dhcpv6_packet_print(struct dhcpv6_packet *pkt, void (*print)(const char *fmt, ...))
{
	static const char *type_name[] = {
		"Solicit",
		"Advertise",
		"Request",
		"Confirt",
		"Renew",
		"Rebind",
		"Reply",
		"Release",
		"Decline",
		"Reconfigure",
		"Information-Request",
		"Relay-Form",
		"Relay-Reply"
	};

	print("[DHCPv6 ");

	if (pkt->hdr->type == 0 || pkt->hdr->type > 13)
		print("Unknown");
	else
		print("%s", type_name[pkt->hdr->type - 1]);
	
	print(" XID=%x", pkt->hdr->trans_id);

	print_options(&pkt->opt_list, 0, print);

	print("]\n");
}

static void print_clientid(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	int i;
	struct dhcpv6_opt_clientid *o = (struct dhcpv6_opt_clientid *)opt->hdr;

	print(" %i:", htons(o->duid.type));
	
	for (i = 0; i < ntohs(o->hdr.len) - 2; i++)
		print("%02x", o->duid.u.raw[i]);
}

static void print_ia_na(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	struct dhcpv6_opt_ia_na *o = (struct dhcpv6_opt_ia_na *)opt->hdr;

	print(" %x T1=%i T2=%i", o->iaid, ntohl(o->T1), ntohl(o->T2));
}

static void print_ia_ta(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	struct dhcpv6_opt_ia_ta *o = (struct dhcpv6_opt_ia_ta *)opt->hdr;

	print(" %x", o->iaid);
}

static void print_ia_addr(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	struct dhcpv6_opt_ia_addr *o = (struct dhcpv6_opt_ia_addr *)opt->hdr;
	char str[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, &o->addr, str, sizeof(str));
	print(" %s pref_lifetime=%i valid_lifetime=%i", str, ntohl(o->pref_lifetime), ntohl(o->valid_lifetime));
}

static void print_oro(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	uint16_t *ptr = (uint16_t *)opt->hdr->data;
	uint16_t *end_ptr = ptr + ntohs(opt->hdr->len)/2;
	struct dict_option *dopt;
	int f = 0;

	for (; ptr < end_ptr; ptr++) {
		if (f)
			print(",");
		else 
			print(" ");

		for (dopt = known_options; dopt->code; dopt++) {
			if (ntohs(*ptr) == dopt->code)
				break;
		}

		if (dopt->code)
			print("%s", dopt->name);
		else
			print("%i", ntohs(*ptr));

		f = 1;
	}
}

static void print_uint8(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	print(" %i", *(uint8_t *)opt->hdr->data);
}

static void print_time(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	print(" %u", *(uint32_t *)opt->hdr->data);
}

static void print_ipv6addr(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	char str[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, opt->hdr->data, str, sizeof(str));

	print(" %s", str);
}

static void print_ipv6addr_array(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	char str[INET6_ADDRSTRLEN];
	int i;
	int f = 0;
	struct in6_addr *addr = (struct in6_addr *)opt->hdr->data;

	for (i = ntohs(opt->hdr->len) / sizeof(*addr); i; i--, addr++) {
		inet_ntop(AF_INET6, addr, str, sizeof(str));
		print("%c%s", f ? ',' : ' ', str);
		f = 1;
	}
}

static void print_status(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	struct dhcpv6_opt_status *o = (struct dhcpv6_opt_status *)opt->hdr;
	static char *status_name[] = {
		"Success",
		"UnspecFail",
		"NoAddrsAvail",
		"NoBindings",
		"NotOnLink",
		"UseMulticast"
	};

	if (ntohs(o->code) < 0 || ntohs(o->code) > 5)
		print(" %u", ntohs(o->code));
	else
		print(" %s", status_name[ntohs(o->code)]);
}

static void print_uint64(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	print(" %llu", *(uint64_t *)opt->hdr->data);
}

static void print_reconf(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{

}

static void print_dnssl(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{

}

static void print_ia_prefix(struct dhcpv6_option *opt, void (*print)(const char *fmt, ...))
{
	struct dhcpv6_opt_ia_prefix *o = (struct dhcpv6_opt_ia_prefix *)opt->hdr;
	char str[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, &o->prefix, str, sizeof(str));
	print(" %s/%i pref_lifetime=%i valid_lifetime=%i", str, o->prefix_len, ntohl(o->pref_lifetime), ntohl(o->valid_lifetime));
}

