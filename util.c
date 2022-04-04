/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <bosjansson.h>
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#ifndef WIN32
#include <fcntl.h>
# ifdef __linux__
#  include <sys/prctl.h>
# endif
# include <sys/socket.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <netdb.h>
#else
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <mmsystem.h>
#endif

#include "algorithm/ethash.h"
#include "miner.h"
#include "elist.h"
#include "compat.h"
#include "util.h"
#include "pool.h"

#define DEFAULT_SOCKWAIT 60
extern double opt_diff_mult;
extern void suffix_string_double(double val, char *buf, size_t bufsiz, int sigdigits);

bool successful_connect = false;
static void keep_sockalive(SOCKETTYPE fd)
{
  const int tcp_one = 1;
#ifdef __linux
  const int tcp_keepidle = 45;
#endif
#ifndef WIN32
  const int tcp_keepintvl = 30;
  int flags = fcntl(fd, F_GETFL, 0);

  fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
  u_long flags = 1;

  ioctlsocket(fd, FIONBIO, &flags);
#endif

  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&tcp_one, sizeof(tcp_one));
  if (!opt_delaynet)
#ifndef __linux
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&tcp_one, sizeof(tcp_one));
#else /* __linux */
    setsockopt(fd, SOL_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one));
  setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &tcp_one, sizeof(tcp_one));
  setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &tcp_keepidle, sizeof(tcp_keepidle));
  setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &tcp_keepintvl, sizeof(tcp_keepintvl));
#endif /* __linux__ */

#ifdef __APPLE_CC__
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &tcp_keepintvl, sizeof(tcp_keepintvl));
#endif /* __APPLE_CC__ */

}

struct tq_ent {
  void      *data;
  struct list_head  q_node;
};

#ifdef HAVE_LIBCURL
struct timeval nettime;

struct data_buffer {
  void    *buf;
  size_t    len;
};

struct upload_buffer {
  const void  *buf;
  size_t    len;
};

struct header_info {
  char    *lp_path;
  int   rolltime;
  char    *reason;
  char    *stratum_url;
  bool    hadrolltime;
  bool    canroll;
  bool    hadexpire;
};

static void databuf_free(struct data_buffer *db)
{
  if (db) {
    if (db->buf) free(db->buf);
    memset(db, 0, sizeof(*db));
  }
}

static size_t all_data_cb(const void *ptr, size_t size, size_t nmemb,
        void *user_data)
{
  struct data_buffer *db = (struct data_buffer *)user_data;
  size_t len = size * nmemb;
  size_t oldlen, newlen;
  void *newmem;
  static const unsigned char zero = 0;

  if (len > 0) {
    oldlen = db->len;
    newlen = oldlen + len;

    newmem = realloc(db->buf, newlen + 1);
    if (!newmem)
      return 0;

    db->buf = newmem;
    db->len = newlen;
    memcpy((uint8_t*)db->buf + oldlen, ptr, len);
    memcpy((uint8_t*)db->buf + newlen, &zero, 1); /* null terminate */
  }

  return len;
}

static size_t upload_data_cb(void *ptr, size_t size, size_t nmemb,
           void *user_data)
{
  struct upload_buffer *ub = (struct upload_buffer *)user_data;
  unsigned int len = size * nmemb;

  if (len > ub->len)
    len = ub->len;

  if (len > 0) {
    memcpy(ptr, ub->buf, len);
    ub->buf = (uint8_t*)ub->buf + len;
    ub->len -= len;
  }

  return len;
}

static size_t resp_hdr_cb(void *ptr, size_t size, size_t nmemb, void *user_data)
{
  struct header_info *hi = (struct header_info *)user_data;
  size_t remlen, slen, ptrlen = size * nmemb;
  char *rem, *val = NULL, *key = NULL;
  void *tmp;

  val = (char *)calloc(1, ptrlen);
  key = (char *)calloc(1, ptrlen);
  if (!key || !val)
    goto out;

  tmp = memchr(ptr, ':', ptrlen);
  if (!tmp || (tmp == ptr)) /* skip empty keys / blanks */
    goto out;
  slen = (uint8_t*)tmp - (uint8_t*)ptr;
  if ((slen + 1) == ptrlen) /* skip key w/ no value */
    goto out;
  memcpy(key, ptr, slen);   /* store & nul term key */
  key[slen] = 0;

  rem = (char*)ptr + slen + 1;  /* trim value's leading whitespace */
  remlen = ptrlen - slen - 1;
  while ((remlen > 0) && (isspace(*rem))) {
    remlen--;
    rem++;
  }

  memcpy(val, rem, remlen); /* store value, trim trailing ws */
  val[remlen] = 0;
  while ((*val) && (isspace(val[strlen(val) - 1])))
    val[strlen(val) - 1] = 0;

  if (!*val)      /* skip blank value */
    goto out;

  if (opt_protocol)
    applog(LOG_DEBUG, "HTTP hdr(%s): %s", key, val);

  if (!strcasecmp("X-Roll-Ntime", key)) {
    hi->hadrolltime = true;
    if (!strncasecmp("N", val, 1))
      applog(LOG_DEBUG, "X-Roll-Ntime: N found");
    else {
      hi->canroll = true;

      /* Check to see if expire= is supported and if not, set
       * the rolltime to the default scantime */
      if (strlen(val) > 7 && !strncasecmp("expire=", val, 7)) {
        sscanf(val + 7, "%d", &hi->rolltime);
        hi->hadexpire = true;
      } else
        hi->rolltime = opt_scantime;
      applog(LOG_DEBUG, "X-Roll-Ntime expiry set to %d", hi->rolltime);
    }
  }

  if (!strcasecmp("X-Long-Polling", key)) {
    hi->lp_path = val;  /* steal memory reference */
    val = NULL;
  }

  if (!strcasecmp("X-Reject-Reason", key)) {
    hi->reason = val; /* steal memory reference */
    val = NULL;
  }

  if (!strcasecmp("X-Stratum", key)) {
    hi->stratum_url = val;
    val = NULL;
  }

out:
  free(key);
  free(val);
  return ptrlen;
}

static void last_nettime(struct timeval *last)
{
  rd_lock(&netacc_lock);
  last->tv_sec = nettime.tv_sec;
  last->tv_usec = nettime.tv_usec;
  rd_unlock(&netacc_lock);
}

static void set_nettime(void)
{
  wr_lock(&netacc_lock);
  cgtime(&nettime);
  wr_unlock(&netacc_lock);
}

#if CURL_HAS_KEEPALIVE
static void keep_curlalive(CURL *curl)
{
  const long int keepalive = 1;

  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, keepalive);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, opt_tcp_keepalive);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, opt_tcp_keepalive);
}
#else
static void keep_curlalive(CURL *curl)
{
  SOCKETTYPE sock;

  curl_easy_getinfo(curl, CURLINFO_LASTSOCKET, (long *)&sock);
  keep_sockalive(sock);
}
#endif

static int curl_debug_cb(__maybe_unused CURL *handle, curl_infotype type,
       __maybe_unused char *data, size_t size, void *userdata)
{
  struct pool *pool = (struct pool *)userdata;

  switch(type) {
    case CURLINFO_HEADER_IN:
    case CURLINFO_DATA_IN:
    case CURLINFO_SSL_DATA_IN:
      pool->sgminer_pool_stats.net_bytes_received += size;
      break;
    case CURLINFO_HEADER_OUT:
    case CURLINFO_DATA_OUT:
    case CURLINFO_SSL_DATA_OUT:
      pool->sgminer_pool_stats.net_bytes_sent += size;
      break;
    case CURLINFO_TEXT:
    default:
      break;
  }
  return 0;
}

json_t *json_rpc_call(CURL *curl, char *curl_err_str, const char *url,
          const char *userpass, const char *rpc_req,
          bool probe, bool longpoll, int *rolltime,
          struct pool *pool, bool share)
{
  long timeout = longpoll ? (60 * 60) : 60;
  struct data_buffer all_data = {NULL, 0};
  struct header_info hi = {NULL, 0, NULL, NULL, false, false, false};
  char len_hdr[64], user_agent_hdr[128];
  struct curl_slist *headers = NULL;
  struct upload_buffer upload_data;
  json_t *val, *err_val, *res_val;
  bool probing = false;
  double byte_count;
  json_error_t err;
  int rc;

  memset(&err, 0, sizeof(err));

  /* it is assumed that 'curl' is freshly [re]initialized at this pt */

  if (probe)
    probing = !pool->probed;
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

  // CURLOPT_VERBOSE won't write to stderr if we use CURLOPT_DEBUGFUNCTION
  curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
  curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void *)pool);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

  /* Shares are staggered already and delays in submission can be costly
   * so do not delay them */
  if (!opt_delaynet || share)
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, all_data_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &all_data);
  curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_data_cb);
  curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, resp_hdr_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hi);
  curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
  if (pool->rpc_proxy) {
    curl_easy_setopt(curl, CURLOPT_PROXY, pool->rpc_proxy);
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, pool->rpc_proxytype);
  } else if (opt_socks_proxy) {
    curl_easy_setopt(curl, CURLOPT_PROXY, opt_socks_proxy);
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
  }
  if (userpass) {
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpass);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
  }
  if (longpoll)
    keep_curlalive(curl);
  curl_easy_setopt(curl, CURLOPT_POST, 1);

  if (opt_protocol)
    applog(LOG_DEBUG, "JSON protocol request:\n%s", rpc_req);

  upload_data.buf = rpc_req;
  upload_data.len = strlen(rpc_req);
  sprintf(len_hdr, "Content-Length: %lu",
    (unsigned long) upload_data.len);
  sprintf(user_agent_hdr, "User-Agent: %s", PACKAGE_STRING);

  headers = curl_slist_append(headers,
    "Content-type: application/json");
  headers = curl_slist_append(headers,
    "X-Mining-Extensions: longpoll midstate rollntime submitold");

  if (likely(global_hashrate)) {
    char ghashrate[255];

    sprintf(ghashrate, "X-Mining-Hashrate: %llu", global_hashrate);
    headers = curl_slist_append(headers, ghashrate);
  }

  headers = curl_slist_append(headers, len_hdr);
  headers = curl_slist_append(headers, user_agent_hdr);
  headers = curl_slist_append(headers, "Expect:"); /* disable Expect hdr*/

  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  if (opt_delaynet) {
    /* Don't delay share submission, but still track the nettime */
    if (!share) {
      long long now_msecs, last_msecs;
      struct timeval now, last;

      cgtime(&now);
      last_nettime(&last);
      now_msecs = (long long)now.tv_sec * 1000;
      now_msecs += now.tv_usec / 1000;
      last_msecs = (long long)last.tv_sec * 1000;
      last_msecs += last.tv_usec / 1000;
      if (now_msecs > last_msecs && now_msecs - last_msecs < 250) {
        struct timespec rgtp;

        rgtp.tv_sec = 0;
        rgtp.tv_nsec = (250 - (now_msecs - last_msecs)) * 1000000;
        nanosleep(&rgtp, NULL);
      }
    }
    set_nettime();
  }

  rc = curl_easy_perform(curl);
  if (rc) {
    applog(LOG_INFO, "HTTP request failed: %s", curl_err_str);
    goto err_out;
  }

  if (!all_data.buf) {
    applog(LOG_DEBUG, "Empty data received in json_rpc_call.");
    goto err_out;
  }

  pool->sgminer_pool_stats.times_sent++;
  if (curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &byte_count) == CURLE_OK)
    pool->sgminer_pool_stats.bytes_sent += byte_count;
  pool->sgminer_pool_stats.times_received++;
  if (curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &byte_count) == CURLE_OK)
    pool->sgminer_pool_stats.bytes_received += byte_count;

  if (probing) {
    pool->probed = true;
    /* If X-Long-Polling was found, activate long polling */
    if (hi.lp_path) {
      if (pool->hdr_path != NULL)
        free(pool->hdr_path);
      pool->hdr_path = hi.lp_path;
    } else
      pool->hdr_path = NULL;
    if (hi.stratum_url) {
      pool->stratum_url = hi.stratum_url;
      hi.stratum_url = NULL;
    }
  } else {
    if (hi.lp_path) {
      free(hi.lp_path);
      hi.lp_path = NULL;
    }
    if (hi.stratum_url) {
      free(hi.stratum_url);
      hi.stratum_url = NULL;
    }
  }

  *rolltime = hi.rolltime;
  pool->sgminer_pool_stats.rolltime = hi.rolltime;
  pool->sgminer_pool_stats.hadrolltime = hi.hadrolltime;
  pool->sgminer_pool_stats.canroll = hi.canroll;
  pool->sgminer_pool_stats.hadexpire = hi.hadexpire;

  val = JSON_LOADS((const char *)all_data.buf, &err);
  if (!val) {
    applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);

    if (opt_protocol)
      applog(LOG_DEBUG, "JSON protocol response:\n%s", (char *)(all_data.buf));

    goto err_out;
  }

  if (opt_protocol) {
    char *s = json_dumps(val, JSON_INDENT(3));

    applog(LOG_DEBUG, "JSON protocol response:\n%s", s);
    free(s);
  }

  /* JSON-RPC valid response returns a non-null 'result',
   * and a null 'error'.
   */
  res_val = json_object_get(val, "result");
  err_val = json_object_get(val, "error");

  if (!res_val ||(err_val && !json_is_null(err_val))) {
    char *s;

    if (err_val)
      s = json_dumps(err_val, JSON_INDENT(3));
    else
      s = strdup("(unknown reason)");

    applog(LOG_INFO, "JSON-RPC call failed: %s", s);

    free(s);

    goto err_out;
  }

  if (hi.reason) {
    json_object_set_new(val, "reject-reason", json_string(hi.reason));
    free(hi.reason);
    hi.reason = NULL;
  }
  successful_connect = true;
  databuf_free(&all_data);
  curl_slist_free_all(headers);
  curl_easy_reset(curl);
  return val;

err_out:
  databuf_free(&all_data);
  curl_slist_free_all(headers);
  curl_easy_reset(curl);
  if (!successful_connect)
    applog(LOG_DEBUG, "Failed to connect in json_rpc_call");
  curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
  return NULL;
}
#define PROXY_HTTP  CURLPROXY_HTTP
#define PROXY_HTTP_1_0  CURLPROXY_HTTP_1_0
#define PROXY_SOCKS4  CURLPROXY_SOCKS4
#define PROXY_SOCKS5  CURLPROXY_SOCKS5
#define PROXY_SOCKS4A CURLPROXY_SOCKS4A
#define PROXY_SOCKS5H CURLPROXY_SOCKS5_HOSTNAME
#else /* HAVE_LIBCURL */
#define PROXY_HTTP  0
#define PROXY_HTTP_1_0  1
#define PROXY_SOCKS4  2
#define PROXY_SOCKS5  3
#define PROXY_SOCKS4A 4
#define PROXY_SOCKS5H 5
#endif /* HAVE_LIBCURL */

static struct {
  const char *name;
  proxytypes_t proxytype;
} proxynames[] = {
  { "http:",  PROXY_HTTP },
  { "http0:", PROXY_HTTP_1_0 },
  { "socks4:",  PROXY_SOCKS4 },
  { "socks5:",  PROXY_SOCKS5 },
  { "socks4a:", PROXY_SOCKS4A },
  { "socks5h:", PROXY_SOCKS5H },
  { NULL, (proxytypes_t)NULL }
};

const char *proxytype(proxytypes_t proxytype)
{
  int i;

  for (i = 0; proxynames[i].name; i++)
    if (proxynames[i].proxytype == proxytype)
      return proxynames[i].name;

  return "invalid";
}

char *get_proxy(char *url, struct pool *pool)
{
  pool->rpc_proxy = NULL;

  char *split;
  int plen, len, i;

  for (i = 0; proxynames[i].name; i++) {
    plen = strlen(proxynames[i].name);
    if (strncmp(url, proxynames[i].name, plen) == 0) {
      if (!(split = strchr(url, '|')))
        return url;

      *split = '\0';
      len = split - url;
      pool->rpc_proxy = (char *)malloc(1 + len - plen);
      if (!(pool->rpc_proxy))
        quithere(1, "Failed to malloc rpc_proxy");

      strcpy(pool->rpc_proxy, url + plen);
      extract_sockaddr(pool->rpc_proxy, &pool->sockaddr_proxy_url, &pool->sockaddr_proxy_port);
      pool->rpc_proxytype = proxynames[i].proxytype;
      url = split + 1;
      break;
    }
  }
  return url;
}

/* Adequate size s==len*2 + 1 must be alloced to use this variant */
void __bin2hex(char *s, const unsigned char *p, size_t len)
{
  int i;
  static const char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  for (i = 0; i < (int)len; i++) {
    *s++ = hex[p[i] >> 4];
    *s++ = hex[p[i] & 0xF];
  }
  *s++ = '\0';
}

/* Returns a malloced array string of a binary value of arbitrary length. The
 * array is rounded up to a 4 byte size to appease architectures that need
 * aligned array  sizes */
char *bin2hex(const unsigned char *p, size_t len)
{
  ssize_t slen;
  char *s;

  slen = len * 2 + 1;
  if (slen % 4)
    slen += 4 - (slen % 4);
  s = (char *)calloc(slen, 1);
  if (unlikely(!s))
    quithere(1, "Failed to calloc");

  __bin2hex(s, p, len);

  return s;
}

/* Does the reverse of bin2hex but does not allocate any ram */
static const int hex2bin_tbl[256] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};
bool hex2bin(unsigned char *p, const char *hexstr, size_t len)
{
  int nibble1, nibble2;
  unsigned char idx;
  bool ret = false;

  while (*hexstr && len) {
    if (unlikely(!hexstr[1])) {
      applog(LOG_ERR, "hex2bin str truncated");
      return ret;
    }

    idx = *hexstr++;
    nibble1 = hex2bin_tbl[idx];
    idx = *hexstr++;
    nibble2 = hex2bin_tbl[idx];

    if (unlikely((nibble1 < 0) || (nibble2 < 0))) {
      applog(LOG_ERR, "hex2bin scan failed");
      return ret;
    }

    *p++ = (((unsigned char)nibble1) << 4) | ((unsigned char)nibble2);
    --len;
  }

  if (likely(len == 0 && *hexstr == 0))
    ret = true;
  return ret;
}

bool eth_hex2bin(unsigned char *p, const char *hexstr, size_t len)
{
  if (hexstr == NULL)
    return false;
  if (hexstr[0] == '0' && hexstr[1] == 'x')
    hexstr += 2;
  memset(p, 0, len);
  len = MIN(len, (strlen(hexstr) + 1) / 2);
  return hex2bin(p, hexstr, len);
}

bool fulltest(const unsigned char *hash, const unsigned char *target)
{
  uint32_t *hash32 = (uint32_t *)hash;
  uint32_t *target32 = (uint32_t *)target;
  bool rc = true;
  int i;

  for (i = 28 / 4; i >= 0; i--) {
    uint32_t h32tmp = le32toh(hash32[i]);
    uint32_t t32tmp = le32toh(target32[i]);

    if (h32tmp > t32tmp) {
      rc = false;
      break;
    }
    if (h32tmp < t32tmp) {
      rc = true;
      break;
    }
  }

  if (opt_debug) {
    unsigned char hash_swap[32], target_swap[32];
    char *hash_str, *target_str;

    swab256(hash_swap, hash);
    swab256(target_swap, target);
    hash_str = bin2hex(hash_swap, 32);
    target_str = bin2hex(target_swap, 32);

    applog(LOG_DEBUG, " Proof: %s\nTarget: %s\nTrgVal? %s",
      hash_str,
      target_str,
      rc ? "YES (hash <= target)" :
           "no (false positive; hash > target)");

    free(hash_str);
    free(target_str);
  }

  return rc;
}

struct thread_q *tq_new(void)
{
  struct thread_q *tq;

  tq = (struct thread_q *)calloc(1, sizeof(*tq));
  if (!tq)
    return NULL;

  INIT_LIST_HEAD(&tq->q);
  pthread_mutex_init(&tq->mutex, NULL);
  pthread_cond_init(&tq->cond, NULL);

  return tq;
}

void tq_free(struct thread_q *tq)
{
  struct tq_ent *ent, *iter;

  if (!tq)
    return;

  list_for_each_entry_safe(ent, iter, &tq->q, q_node) {
    list_del(&ent->q_node);
    free(ent);
  }

  pthread_cond_destroy(&tq->cond);
  pthread_mutex_destroy(&tq->mutex);

  memset(tq, 0, sizeof(*tq)); /* poison */
  free(tq);
}

static void tq_freezethaw(struct thread_q *tq, bool frozen)
{
  mutex_lock(&tq->mutex);
  tq->frozen = frozen;
  pthread_cond_signal(&tq->cond);
  mutex_unlock(&tq->mutex);
}

void tq_freeze(struct thread_q *tq)
{
  tq_freezethaw(tq, true);
}

void tq_thaw(struct thread_q *tq)
{
  tq_freezethaw(tq, false);
}

bool tq_push(struct thread_q *tq, void *data)
{
  struct tq_ent *ent;
  bool rc = true;

  ent = (struct tq_ent *)calloc(1, sizeof(*ent));
  if (!ent)
    return false;

  ent->data = data;
  INIT_LIST_HEAD(&ent->q_node);

  mutex_lock(&tq->mutex);
  if (!tq->frozen) {
    list_add_tail(&ent->q_node, &tq->q);
  } else {
    free(ent);
    rc = false;
  }
  pthread_cond_signal(&tq->cond);
  mutex_unlock(&tq->mutex);

  return rc;
}

void *tq_pop(struct thread_q *tq, const struct timespec *abstime)
{
  struct tq_ent *ent;
  void *rval = NULL;
  int rc;

  mutex_lock(&tq->mutex);
  if (!list_empty(&tq->q))
    goto pop;

  if (abstime)
    rc = pthread_cond_timedwait(&tq->cond, &tq->mutex, abstime);
  else
    rc = pthread_cond_wait(&tq->cond, &tq->mutex);
  if (rc)
    goto out;
  if (list_empty(&tq->q))
    goto out;
pop:
  ent = list_entry(tq->q.next, struct tq_ent*, q_node);
  rval = ent->data;

  list_del(&ent->q_node);
  free(ent);
out:
  mutex_unlock(&tq->mutex);

  return rval;
}

int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg)
{
  cgsem_init(&thr->sem);

  return pthread_create(&thr->pth, attr, start, arg);
}

void thr_info_cancel_join(struct thr_info *thr)
{
  if (!thr)
    return;

  if (PTH(thr) != 0L) {
    pthread_cancel(thr->pth);
    pthread_join(thr->pth, NULL);
    PTH(thr) = 0L;
  }
  cgsem_destroy(&thr->sem);
}

void subtime(struct timeval *a, struct timeval *b)
{
  timersub(a, b, b);
}

void addtime(struct timeval *a, struct timeval *b)
{
  timeradd(a, b, b);
}

bool time_more(struct timeval *a, struct timeval *b)
{
  return timercmp(a, b, >);
}

bool time_less(struct timeval *a, struct timeval *b)
{
  return timercmp(a, b, <);
}

void copy_time(struct timeval *dest, const struct timeval *src)
{
  memcpy(dest, src, sizeof(struct timeval));
}

void timespec_to_val(struct timeval *val, const struct timespec *spec)
{
  val->tv_sec = spec->tv_sec;
  val->tv_usec = spec->tv_nsec / 1000;
}

void timeval_to_spec(struct timespec *spec, const struct timeval *val)
{
  spec->tv_sec = val->tv_sec;
  spec->tv_nsec = val->tv_usec * 1000;
}

void us_to_timeval(struct timeval *val, int64_t us)
{
  lldiv_t tvdiv = lldiv(us, 1000000);

  val->tv_sec = tvdiv.quot;
  val->tv_usec = tvdiv.rem;
}

void us_to_timespec(struct timespec *spec, int64_t us)
{
  lldiv_t tvdiv = lldiv(us, 1000000);

  spec->tv_sec = tvdiv.quot;
  spec->tv_nsec = tvdiv.rem * 1000;
}

void ms_to_timespec(struct timespec *spec, int64_t ms)
{
  lldiv_t tvdiv = lldiv(ms, 1000);

  spec->tv_sec = tvdiv.quot;
  spec->tv_nsec = tvdiv.rem * 1000000;
}

void ms_to_timeval(struct timeval *val, int64_t ms)
{
  lldiv_t tvdiv = lldiv(ms, 1000);

  val->tv_sec = tvdiv.quot;
  val->tv_usec = tvdiv.rem * 1000;
}

void timeraddspec(struct timespec *a, const struct timespec *b)
{
  a->tv_sec += b->tv_sec;
  a->tv_nsec += b->tv_nsec;
  if (a->tv_nsec >= 1000000000) {
    a->tv_nsec -= 1000000000;
    a->tv_sec++;
  }
}

static int __maybe_unused timespec_to_ms(struct timespec *ts)
{
  return ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

/* Subtract b from a */
static void __maybe_unused timersubspec(struct timespec *a, const struct timespec *b)
{
  a->tv_sec -= b->tv_sec;
  a->tv_nsec -= b->tv_nsec;
  if (a->tv_nsec < 0) {
    a->tv_nsec += 1000000000;
    a->tv_sec--;
  }
}

/* These are sgminer specific sleep functions that use an absolute nanosecond
 * resolution timer to avoid poor usleep accuracy and overruns. */
#ifdef WIN32
/* Windows start time is since 1601 LOL so convert it to unix epoch 1970. */
#define EPOCHFILETIME (116444736000000000LL)

/* Return the system time as an lldiv_t in decimicroseconds. */
static void decius_time(lldiv_t *lidiv)
{
  FILETIME ft;
  LARGE_INTEGER li;

  GetSystemTimeAsFileTime(&ft);
  li.LowPart  = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  li.QuadPart -= EPOCHFILETIME;

  /* SystemTime is in decimicroseconds so divide by an unusual number */
  *lidiv = lldiv(li.QuadPart, 10000000);
}

/* This is a sgminer gettimeofday wrapper. Since we always call gettimeofday
 * with tz set to NULL, and windows' default resolution is only 15ms, this
 * gives us higher resolution times on windows. */
void cgtime(struct timeval *tv)
{
  lldiv_t lidiv;

  decius_time(&lidiv);
  tv->tv_sec = lidiv.quot;
  tv->tv_usec = lidiv.rem / 10;
}

#else /* WIN32 */
void cgtime(struct timeval *tv)
{
  gettimeofday(tv, NULL);
}

int cgtimer_to_ms(cgtimer_t *cgt)
{
  return timespec_to_ms(cgt);
}

/* Subtracts b from a and stores it in res. */
void cgtimer_sub(cgtimer_t *a, cgtimer_t *b, cgtimer_t *res)
{
  res->tv_sec = a->tv_sec - b->tv_sec;
  res->tv_nsec = a->tv_nsec - b->tv_nsec;
  if (res->tv_nsec < 0) {
    res->tv_nsec += 1000000000;
    res->tv_sec--;
  }
}
#endif /* WIN32 */

#ifdef CLOCK_MONOTONIC /* Essentially just linux */
void cgtimer_time(cgtimer_t *ts_start)
{
  clock_gettime(CLOCK_MONOTONIC, ts_start);
}

static void nanosleep_abstime(struct timespec *ts_end)
{
  int ret;

  do {
    ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts_end, NULL);
  } while (ret == EINTR);
}

/* Reentrant version of cgsleep functions allow start time to be set separately
 * from the beginning of the actual sleep, allowing scheduling delays to be
 * counted in the sleep. */
void cgsleep_ms_r(cgtimer_t *ts_start, int ms)
{
  struct timespec ts_end;

  ms_to_timespec(&ts_end, ms);
  timeraddspec(&ts_end, ts_start);
  nanosleep_abstime(&ts_end);
}

void cgsleep_us_r(cgtimer_t *ts_start, int64_t us)
{
  struct timespec ts_end;

  us_to_timespec(&ts_end, us);
  timeraddspec(&ts_end, ts_start);
  nanosleep_abstime(&ts_end);
}
#else /* CLOCK_MONOTONIC */
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
void cgtimer_time(cgtimer_t *ts_start)
{
  clock_serv_t cclock;
  mach_timespec_t mts;

  host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  ts_start->tv_sec = mts.tv_sec;
  ts_start->tv_nsec = mts.tv_nsec;
}
#elif !defined(WIN32) /* __MACH__ - Everything not linux/macosx/win32 */
void cgtimer_time(cgtimer_t *ts_start)
{
  struct timeval tv;

  cgtime(&tv);
  ts_start->tv_sec = tv->tv_sec;
  ts_start->tv_nsec = tv->tv_usec * 1000;
}
#endif /* __MACH__ */

#ifdef WIN32
/* For windows we use the SystemTime stored as a LARGE_INTEGER as the cgtimer_t
 * typedef, allowing us to have sub-microsecond resolution for times, do simple
 * arithmetic for timer calculations, and use windows' own hTimers to get
 * accurate absolute timeouts. */
int cgtimer_to_ms(cgtimer_t *cgt)
{
  return (int)(cgt->QuadPart / 10000LL);
}

/* Subtracts b from a and stores it in res. */
void cgtimer_sub(cgtimer_t *a, cgtimer_t *b, cgtimer_t *res)
{
  res->QuadPart = a->QuadPart - b->QuadPart;
}

/* Note that cgtimer time is NOT offset by the unix epoch since we use absolute
 * timeouts with hTimers. */
void cgtimer_time(cgtimer_t *ts_start)
{
  FILETIME ft;

  GetSystemTimeAsFileTime(&ft);
  ts_start->LowPart = ft.dwLowDateTime;
  ts_start->HighPart = ft.dwHighDateTime;
}

static void liSleep(LARGE_INTEGER *li, int timeout)
{
  HANDLE hTimer;
  DWORD ret;

  if (unlikely(timeout <= 0))
    return;

  hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
  if (unlikely(!hTimer))
    quit(1, "Failed to create hTimer in liSleep");
  ret = SetWaitableTimer(hTimer, li, 0, NULL, NULL, 0);
  if (unlikely(!ret))
    quit(1, "Failed to SetWaitableTimer in liSleep");
  /* We still use a timeout as a sanity check in case the system time
   * is changed while we're running */
  ret = WaitForSingleObject(hTimer, timeout);
  if (unlikely(ret != WAIT_OBJECT_0 && ret != WAIT_TIMEOUT))
    quit(1, "Failed to WaitForSingleObject in liSleep");
  CloseHandle(hTimer);
}

void cgsleep_ms_r(cgtimer_t *ts_start, int ms)
{
  LARGE_INTEGER li;

  li.QuadPart = ts_start->QuadPart + (int64_t)ms * 10000LL;
  liSleep(&li, ms);
}

void cgsleep_us_r(cgtimer_t *ts_start, int64_t us)
{
  LARGE_INTEGER li;
  int ms;

  li.QuadPart = ts_start->QuadPart + us * 10LL;
  ms = us / 1000;
  if (!ms)
    ms = 1;
  liSleep(&li, ms);
}
#else /* WIN32 */
static void cgsleep_spec(struct timespec *ts_diff, const struct timespec *ts_start)
{
  struct timespec now;

  timeraddspec(ts_diff, ts_start);
  cgtimer_time(&now);
  timersubspec(ts_diff, &now);
  if (unlikely(ts_diff->tv_sec < 0))
    return;
  nanosleep(ts_diff, NULL);
}

void cgsleep_ms_r(cgtimer_t *ts_start, int ms)
{
  struct timespec ts_diff;

  ms_to_timespec(&ts_diff, ms);
  cgsleep_spec(&ts_diff, ts_start);
}

void cgsleep_us_r(cgtimer_t *ts_start, int64_t us)
{
  struct timespec ts_diff;

  us_to_timespec(&ts_diff, us);
  cgsleep_spec(&ts_diff, ts_start);
}
#endif /* WIN32 */
#endif /* CLOCK_MONOTONIC */

void cgsleep_ms(int ms)
{
  cgtimer_t ts_start;

  cgsleep_prepare_r(&ts_start);
  cgsleep_ms_r(&ts_start, ms);
}

void cgsleep_us(int64_t us)
{
  cgtimer_t ts_start;

  cgsleep_prepare_r(&ts_start);
  cgsleep_us_r(&ts_start, us);
}

/* Returns the microseconds difference between end and start times as a double */
double us_tdiff(struct timeval *end, struct timeval *start)
{
  /* Sanity check. We should only be using this for small differences so
   * limit the max to 60 seconds. */
  if (unlikely(end->tv_sec - start->tv_sec > 60))
    return 60000000;
  return (end->tv_sec - start->tv_sec) * 1000000 + (end->tv_usec - start->tv_usec);
}

/* Returns the milliseconds difference between end and start times */
int ms_tdiff(struct timeval *end, struct timeval *start)
{
  /* Like us_tdiff, limit to 1 hour. */
  if (unlikely(end->tv_sec - start->tv_sec > 3600))
    return 3600000;
  return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_usec - start->tv_usec) / 1000;
}

/* Returns the seconds difference between end and start times as a double */
double tdiff(struct timeval *end, struct timeval *start)
{
  return end->tv_sec - start->tv_sec + (end->tv_usec - start->tv_usec) / 1000000.0;
}

bool extract_sockaddr(char *url, char **sockaddr_url, char **sockaddr_port)
{
  char *url_begin, *url_end, *ipv6_begin, *ipv6_end, *port_start = NULL;
  char url_address[256], port[6];
  int url_len, port_len = 0;

  *sockaddr_url = url;
  url_begin = strstr(url, "//");
  if (!url_begin)
    url_begin = url;
  else
    url_begin += 2;

  /* Look for numeric ipv6 entries */
  ipv6_begin = strstr(url_begin, "[");
  ipv6_end = strstr(url_begin, "]");
  if (ipv6_begin && ipv6_end && ipv6_end > ipv6_begin)
    url_end = strstr(ipv6_end, ":");
  else
    url_end = strstr(url_begin, ":");
  if (url_end) {
    url_len = url_end - url_begin;
    port_len = strlen(url_begin) - url_len - 1;
    if (port_len < 1)
      return false;
    port_start = url_end + 1;
  } else
    url_len = strlen(url_begin);

  if (url_len < 1)
    return false;

  if (url_len >= sizeof(url_address))
  {
    applog(LOG_WARNING, "%s: Truncating overflowed address '%.*s'",
           __func__, url_len, url_begin);
    url_len = sizeof(url_address) - 1;
  }

  sprintf(url_address, "%.*s", url_len, url_begin);

  if (port_len) {
    char *slash;

    snprintf(port, 6, "%.*s", port_len, port_start);
    slash = strchr(port, '/');
    if (slash)
      *slash = '\0';
  } else
    strcpy(port, "80");

  *sockaddr_port = strdup(port);
  *sockaddr_url = strdup(url_address);

  return true;
}

enum send_ret {
  SEND_OK,
  SEND_SELECTFAIL,
  SEND_SENDFAIL,
  SEND_INACTIVE
};

/* Send a single command across a socket, appending \n to it. This should all
 * be done under stratum lock except when first establishing the socket */
static enum send_ret __stratum_send_bos(struct pool *pool, char *s, ssize_t len)
{

  SOCKETTYPE sock = pool->sock;
  ssize_t ssent = 0;

  if (opt_protocol) {
    applog(LOG_DEBUG, "SEND: %s", s);
  }
  
//  strcat(s, "\n");
  len;

  while (len > 0 ) {
    struct timeval timeout = {1, 0};
    ssize_t sent;
    fd_set wd;
retry:
    FD_ZERO(&wd);
    FD_SET(sock, &wd);
    if (select(sock + 1, NULL, &wd, NULL, &timeout) < 1) {
      if (interrupted())
        goto retry;
      return SEND_SELECTFAIL;
    }
#ifdef __APPLE__
    sent = send(pool->sock, s + ssent, len, SO_NOSIGPIPE);
#elif WIN32
    sent = send(pool->sock, s + ssent, len, 0);
#else
    sent = send(pool->sock, s + ssent, len, MSG_NOSIGNAL);
#endif
    if (sent < 0) {
      if (!sock_blocks())
        return SEND_SENDFAIL;
      sent = 0;
    }
    ssent += sent;
    len -= sent;
  }

  pool->sgminer_pool_stats.times_sent++;
  pool->sgminer_pool_stats.bytes_sent += ssent;
  pool->sgminer_pool_stats.net_bytes_sent += ssent;
  return SEND_OK;
}

/* Send a single command across a socket, appending \n to it. This should all
 * be done under stratum lock except when first establishing the socket */
static enum send_ret __stratum_send(struct pool *pool, char *s, ssize_t len)
{
  SOCKETTYPE sock = pool->sock;
  ssize_t ssent = 0;

  strcat(s, "\n");
  len++;

  while (len > 0 ) {
    struct timeval timeout = {1, 0};
    ssize_t sent;
    fd_set wd;
retry:
    FD_ZERO(&wd);
    FD_SET(sock, &wd);
    if (select(sock + 1, NULL, &wd, NULL, &timeout) < 1) {
      if (interrupted())
        goto retry;
      return SEND_SELECTFAIL;
    }
#ifdef __APPLE__
    sent = send(pool->sock, s + ssent, len, SO_NOSIGPIPE);
#elif WIN32
    sent = send(pool->sock, s + ssent, len, 0);
#else
    sent = send(pool->sock, s + ssent, len, MSG_NOSIGNAL);
#endif
    if (sent < 0) {
      if (!sock_blocks())
        return SEND_SENDFAIL;
      sent = 0;
    }
    ssent += sent;
    len -= sent;
  }

  pool->sgminer_pool_stats.times_sent++;
  pool->sgminer_pool_stats.bytes_sent += ssent;
  pool->sgminer_pool_stats.net_bytes_sent += ssent;
  return SEND_OK;
}

bool stratum_send(struct pool *pool, char *s, ssize_t len)
{
  enum send_ret ret = SEND_INACTIVE;

  if (opt_protocol)
    applog(LOG_DEBUG, "SEND: %s", s);

  mutex_lock(&pool->stratum_lock);
  if (pool->stratum_active)
    ret = __stratum_send(pool, s, len);
  mutex_unlock(&pool->stratum_lock);

  /* This is to avoid doing applog under stratum_lock */
  switch (ret) {
    default:
    case SEND_OK:
      break;
    case SEND_SELECTFAIL:
      applog(LOG_DEBUG, "Write select failed on %s sock", get_pool_name(pool));
      suspend_stratum(pool);
      break;
    case SEND_SENDFAIL:
      applog(LOG_DEBUG, "Failed to send in stratum_send");
      suspend_stratum(pool);
      break;
    case SEND_INACTIVE:
      applog(LOG_DEBUG, "Stratum send failed due to no pool stratum_active");
      break;
  }
  return (ret == SEND_OK);
}

bool stratum_send_bos(struct pool *pool, char *s, ssize_t len)
{
	enum send_ret ret = SEND_INACTIVE;

	mutex_lock(&pool->stratum_lock);
	if (pool->stratum_active)
		ret = __stratum_send_bos(pool, s, len);
	mutex_unlock(&pool->stratum_lock);

	/* This is to avoid doing applog under stratum_lock */
	switch (ret) {
	default:
	case SEND_OK:
		break;
	case SEND_SELECTFAIL:
		applog(LOG_DEBUG, "Write select failed on %s sock", get_pool_name(pool));
		suspend_stratum(pool);
		break;
	case SEND_SENDFAIL:
		applog(LOG_DEBUG, "Failed to send in stratum_send");
		suspend_stratum(pool);
		break;
	case SEND_INACTIVE:
		applog(LOG_DEBUG, "Stratum send failed due to no pool stratum_active");
		break;
	}
	return (ret == SEND_OK);
}

static bool socket_full(struct pool *pool, int wait)
{
  SOCKETTYPE sock = pool->sock;
  struct timeval timeout;
  fd_set rd;

  if (unlikely(wait < 0))
    wait = 0;
  FD_ZERO(&rd);
  FD_SET(sock, &rd);
  timeout.tv_usec = 0;
  timeout.tv_sec = wait;
  if (select(sock + 1, &rd, NULL, NULL, &timeout) > 0)
    return true;
  return false;
}

/* Check to see if Santa's been good to you */
bool sock_full(struct pool *pool)
{
  if (strlen(pool->sockbuf))
    return true;

  return (socket_full(pool, 0));
}

static void clear_sockbuf(struct pool *pool)
{
  strcpy(pool->sockbuf, "");
  pool->sockbuf_bossize = 0;
}

static void clear_sock(struct pool *pool)
{
  ssize_t n;

  mutex_lock(&pool->stratum_lock);
  do {
    if (pool->sock)
      n = recv(pool->sock, pool->sockbuf, RECVSIZE, 0);
    else
      n = 0;
  } while (n > 0);
  mutex_unlock(&pool->stratum_lock);

  clear_sockbuf(pool);
}

/* Make sure the pool sockbuf is large enough to cope with any coinbase size
 * by reallocing it to a large enough size rounded up to a multiple of RBUFSIZE
 * and zeroing the new memory */
static void recalloc_sock(struct pool *pool, size_t len)
{
  size_t old, newlen;

  old = strlen(pool->sockbuf);
  newlen = old + len + 1;
  if (newlen < pool->sockbuf_size)
    return;
  newlen = newlen + (RBUFSIZE - (newlen % RBUFSIZE));
  // Avoid potentially recursive locking
  // applog(LOG_DEBUG, "Recallocing pool sockbuf to %d", new);
  pool->sockbuf = (char *)realloc(pool->sockbuf, newlen);
  if (!pool->sockbuf)
    quithere(1, "Failed to realloc pool sockbuf");
  memset(pool->sockbuf + old, 0, newlen - old);
  pool->sockbuf_size = newlen;
}

static void recalloc_sock_bos(struct pool *pool, size_t len)
{
	size_t old, newlen;

	old = pool->sockbuf_bossize;
	newlen = old + len + 1;
	if (newlen < pool->sockbuf_size)
		return;
	newlen = newlen + (RBUFSIZE - (newlen % RBUFSIZE));
	// Avoid potentially recursive locking
	// applog(LOG_DEBUG, "Recallocing pool sockbuf to %d", new);
	
	pool->sockbuf = (char*)realloc(pool->sockbuf, pool->sockbuf_bossize + len);

	if (!pool->sockbuf)
		quithere(1, "Failed to realloc pool sockbuf");

	pool->sockbuf_size = newlen;
}

/* Peeks at a socket to find the first end of line and then reads just that
 * from the socket and returns that as a malloced char */
char *recv_line(struct pool *pool)
{
  char *tok, *sret = NULL;
  ssize_t len, buflen;
  int waited = 0;

  if (!strstr(pool->sockbuf, "\n")) {
    struct timeval rstart, now;

    cgtime(&rstart);
    if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
      applog(LOG_DEBUG, "Timed out waiting for data on socket_full");
      goto out;
    }

    do {
      char s[RBUFSIZE];
      size_t slen;
      ssize_t n;

      memset(s, 0, RBUFSIZE);
      n = recv(pool->sock, s, RECVSIZE, 0);
      if (!n) {
        applog(LOG_DEBUG, "Socket closed waiting in recv_line");
        suspend_stratum(pool);
        break;
      }
      cgtime(&now);
      waited = tdiff(&now, &rstart);
      if (n < 0) {
        if (!sock_blocks() || !socket_full(pool, DEFAULT_SOCKWAIT - waited)) {
          applog(LOG_DEBUG, "Failed to recv sock in recv_line");
          suspend_stratum(pool);
          break;
        }
      } else {
        slen = strlen(s);
        recalloc_sock(pool, slen);
        strcat(pool->sockbuf, s);
      }
    } while (waited < DEFAULT_SOCKWAIT && !strstr(pool->sockbuf, "\n"));
  }

  buflen = strlen(pool->sockbuf);

  if ((tok = strtok(pool->sockbuf, "\n")) == NULL) {
    applog(LOG_DEBUG, "Failed to parse a \\n terminated string in recv_line: buffer = %s", pool->sockbuf);
    goto out;
  }
  sret = strdup(tok);
  len = strlen(sret);

  /* Copy what's left in the buffer after the \n, including the
   * terminating \0 */
  if (buflen > len + 1)
    memmove(pool->sockbuf, pool->sockbuf + len + 1, buflen - len + 1);
  else
    strcpy(pool->sockbuf, "");

  pool->sgminer_pool_stats.times_received++;
  pool->sgminer_pool_stats.bytes_received += len;
  pool->sgminer_pool_stats.net_bytes_received += len;
out:
  if (!sret)
    clear_sock(pool);
  else if (opt_protocol)
    applog(LOG_DEBUG, "RECVD: %s", sret);
  return sret;
}

json_t* recode_message(json_t *MyObject2)
{
	size_t size;
	bool istarget = false;
	const char *key;
	json_t *value;
        void *tmp;
	json_t *MyObject = json_object();
	json_object_foreach_safe(MyObject2, tmp, key, value) {
/*
		if (!strcmp(key, "method"))
			if (!strcmp(json_string_value(value), "mining.set_target") ||
				!strcmp(json_string_value(value), "mining.notify")
				) {
				istarget = false;
			}
*/

		if (json_is_null(value))
			json_object_set_new(MyObject, key, value); 

		if (json_is_string(value)) {
      json_object_set_new(MyObject, key, value);
    }
			
		if (json_is_integer(value))
			json_object_set_new(MyObject, key, value);

		if (json_is_boolean(value))
			json_object_set_new(MyObject, key, value);

		if (json_is_array(value)) {
			json_t *json_arr = json_array();
			json_object_set_new(MyObject, key, json_arr);
			size_t index;
			json_t *value2 = NULL;
			json_array_foreach(value, index, value2) {

				if (!istarget) {
					if (json_is_bytes(value2)) {
						int zsize = json_bytes_size(value2);
						unsigned char* zbyte = (unsigned char*)json_bytes_value(value2);
						char* strval = (char*)malloc(zsize * 2 + 1);
						for (int k = 0; k<zsize; k++)
							sprintf(&strval[2 * k], "%02x", zbyte[k]);

						json_array_append_new(json_arr, json_string(strval));
						free(strval);
            json_decref(value2);
					}
				}
				if (json_is_string(value2)) {
					json_array_append_new(json_arr, value2);
				}
				if (json_is_boolean(value2)) {
					json_array_append_new(json_arr, value2);
				}
				if (json_is_array(value2)) {
					size_t index2;
					json_t *value3;
					json_t *json_arr2 = json_array();
					json_array_append_new(json_arr, json_arr2);
					json_array_foreach(value2, index2, value3) {
						if (!istarget) {
							if (json_is_bytes(value3)) {
								int zsize = json_bytes_size(value3);
								unsigned char* zbyte = (unsigned char*)json_bytes_value(value3);
								char* strval = (char*)malloc(zsize * 2 + 1);
								//	  for (int k = 0; k<zsize; k++)
								//		sprintf(&strval[2 * k], "%02x", zbyte[zsize - 1 - k]);
								for (int k = 0; k<zsize; k++)
									sprintf(&strval[2 * k], "%02x", zbyte[k]);

								json_array_append_new(json_arr2, json_string(strval));
								free(strval);
								json_decref(value3);
							}
						}
            //json_decref(value3);
					}
					//							json_t *json_arr2 = json_array();
					//							json_array_append(json_arr, json_arr2);
          json_decref(value2);
				}
        //json_decref(value2);
			}
      json_decref(value);
		}
    //json_decref(value);
	}
	return MyObject;
}



char *recv_line_bos(struct pool *pool)
{

	char *tok, *sret = NULL;
	ssize_t len, buflen;
	int waited = 0;
	json_t *MyObject2 = NULL;
	json_t *MyObject = NULL;
	uint32_t bossize = 0;

	bool istarget = false;
	if (!strstr(pool->sockbuf, "\n")) {
		struct timeval rstart, now;
	
		cgtime(&rstart);

		if (pool->sockbuf_bossize==0)
		if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
			applog(LOG_DEBUG, "Timed out waiting for data on socket_full");
			goto out;
		}

		do {
			char s[RBUFSIZE];
			size_t slen;
			ssize_t n;

			memset(s, 0, RBUFSIZE);
			n = recv(pool->sock, s, RECVSIZE, 0);
		
			if (!n) {
		
				applog(LOG_DEBUG, "Socket closed waiting in recv_line");
			//	suspend_stratum(pool);
				break;
			}
			cgtime(&now);
			waited = tdiff(&now, &rstart);
			if (n < 0) {
			
				if (pool->sockbuf_bossize !=0)
					break;
				else if (!sock_blocks() || !socket_full(pool, 5 - waited)) {
				
					applog(LOG_DEBUG, "Failed to recv sock in recv_line");
					suspend_stratum(pool);
					break;
				}

			}
			else {
			
				recalloc_sock_bos(pool, n);
				memcpy(pool->sockbuf + pool->sockbuf_bossize, s, n);
				pool->sockbuf_bossize += n;
			}
		
		} while (waited < DEFAULT_SOCKWAIT && !strstr(pool->sockbuf, "\n"));
	}


	len = pool->sockbuf_bossize;

		json_error_t boserror;
		if (bos_sizeof(pool->sockbuf) < pool->sockbuf_bossize) {
			//				MyObject2 = bos_deserialize(s + bos_sizeof(s), boserror);
			MyObject2 = bos_deserialize(pool->sockbuf, &boserror);
		}
		else if (bos_sizeof(pool->sockbuf) > pool->sockbuf_bossize)
			applog(LOG_ERR, "missing something in message \n");
		else
			MyObject2 = bos_deserialize(pool->sockbuf, &boserror);
		  MyObject = recode_message(MyObject2);
      //if (MyObject2) json_decref(MyObject2);

	if (bos_sizeof(pool->sockbuf)<pool->sockbuf_bossize) {
		uint32_t totsize = pool->sockbuf_bossize;
		uint32_t remsize = pool->sockbuf_bossize - bos_sizeof(pool->sockbuf);
		uint32_t currsize = bos_sizeof(pool->sockbuf);
		memmove(pool->sockbuf, pool->sockbuf + currsize, remsize);
		pool->sockbuf_bossize = remsize;
	}
	else {
		pool->sockbuf[0] = '\0';
		pool->sockbuf_bossize = 0;
	}


	pool->sgminer_pool_stats.times_received++;
	pool->sgminer_pool_stats.bytes_received += len;
	pool->sgminer_pool_stats.net_bytes_received += len;
out:
	if (MyObject==NULL)
			clear_sock(pool);
	if (opt_protocol)
		applog(LOG_DEBUG, "RECVD: %s", json_dumps(MyObject, 0));
	char *ret = json_dumps(MyObject, 0);
        
        if (MyObject) json_decref(MyObject);
        if (MyObject2) json_decref(MyObject2);
        return ret;
}

/* Extracts a string value from a json array with error checking. To be used
 * when the value of the string returned is only examined and not to be stored.
 * See json_array_string below */
static char *__json_array_string(json_t *val, unsigned int entry)
{
  json_t *arr_entry;

  if (json_is_null(val))
    return NULL;
  if (!json_is_array(val))
    return NULL;
  if (entry > json_array_size(val))
    return NULL;
  arr_entry = json_array_get(val, entry);
  if (!json_is_string(arr_entry))
    return NULL;

  return (char *)json_string_value(arr_entry);
}

/* Creates a freshly malloced dup of __json_array_string */
static char *json_array_string(json_t *val, unsigned int entry)
{
  char *buf = __json_array_string(val, entry);

  if (buf)
    return strdup(buf);
  return NULL;
}

static char *workpadding = "000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000";
static char *workpadding_l2zz = "00000080000000000000000080020000";
static char *blank_merkel = "0000000000000000000000000000000000000000000000000000000000000000";

static bool parse_notify(struct pool *pool, json_t *val)
{
  char *job_id, *prev_hash, *coinbase1, *coinbase2, *bbversion, *nbit,
       *ntime, *header;
  char *roots = NULL;
  size_t cb1_len, cb2_len, alloc_len;
  unsigned char *cb1, *cb2;
  bool clean, ret = false;
  int merkles, i = 0;
  json_t *arr;

  bool has_roots = json_array_size(val) == 10 && (pool->algorithm.type == ALGO_PHI2 || pool->algorithm.type == ALGO_PHI2_NAVI || pool->algorithm.type == ALGO_LYRA2ZZ);

  job_id = json_array_string(val, i++);
  prev_hash = json_array_string(val, i++);
  if (has_roots && (pool->algorithm.type == ALGO_PHI2 || pool->algorithm.type == ALGO_PHI2_NAVI))
    roots = json_array_string(val, i++);
  coinbase1 = json_array_string(val, i++);
  coinbase2 = json_array_string(val, i++);

  arr = json_array_get(val, i++);
  if (!arr || !json_is_array(arr))
    goto out;
  merkles = json_array_size(arr);

  bbversion = json_array_string(val, i++);
  nbit = json_array_string(val, i++);
  ntime = json_array_string(val, i++);
  clean = json_is_true(json_array_get(val, i));
  if (has_roots && pool->algorithm.type == ALGO_LYRA2ZZ)
    roots = json_array_string(val, 9);

  if (!job_id || !prev_hash || !coinbase1 || !coinbase2 || !bbversion || !nbit || !ntime) {
    /* Annoying but we must not leak memory */
    if (job_id)
      free(job_id);
    if (prev_hash)
      free(prev_hash);
    if (roots)
      free(roots);
    if (coinbase1)
      free(coinbase1);
    if (coinbase2)
      free(coinbase2);
    if (bbversion)
      free(bbversion);
    if (nbit)
      free(nbit);
    if (ntime)
      free(ntime);
    goto out;
  }

  cg_wlock(&pool->data_lock);
  if (pool->swork.job_id!= NULL) {
		pool->swork.prev_job_id = pool->swork.job_id;
	}
  free(pool->swork.job_id);
  free(pool->swork.prev_hash);
  free(pool->swork.bbversion);
  free(pool->swork.nbit);
  free(pool->swork.ntime);
  pool->swork.job_id = job_id;
  pool->swork.prev_hash = prev_hash;
  cb1_len = strlen(coinbase1) / 2;
  cb2_len = strlen(coinbase2) / 2;
  pool->swork.bbversion = bbversion;
  pool->swork.nbit = nbit;
  pool->swork.ntime = ntime;
  pool->swork.clean = clean;
  if (pool->next_diff > 0) {
    pool->swork.diff = pool->next_diff;
  }
  alloc_len = pool->swork.cb_len = cb1_len + pool->n1_len + pool->n2size + cb2_len;
  pool->nonce2_offset = cb1_len + pool->n1_len;

  for (i = 0; i < pool->swork.merkles; i++)
    free(pool->swork.merkle_bin[i]);
  if (merkles) {
    pool->swork.merkle_bin = (unsigned char **)realloc(pool->swork.merkle_bin,
             sizeof(char *) * merkles + 1);
    for (i = 0; i < merkles; i++) {
      char *merkle = json_array_string(arr, i);

      pool->swork.merkle_bin[i] = (unsigned char *)malloc(32);
      if (unlikely(!pool->swork.merkle_bin[i]))
        quit(1, "Failed to malloc pool swork merkle_bin");
      hex2bin(pool->swork.merkle_bin[i], merkle, 32);
      free(merkle);
    }
  }
  pool->swork.merkles = merkles;
  if (clean)
    pool->nonce2 = 0;
  pool->merkle_offset = strlen(pool->swork.bbversion) +
            strlen(pool->swork.prev_hash);
  pool->swork.header_len = pool->merkle_offset +
  /* merkle_hash */  32 +
         strlen(pool->swork.ntime) +
         strlen(pool->swork.nbit) +
  /* nonce */    8 +
  /* workpadding */  96;
  pool->merkle_offset /= 2;
  if (roots)
    pool->swork.header_len += strlen(roots); // 128
  // todo: seems too big, useless memory
  pool->swork.header_len = pool->swork.header_len * 2 + 1;
  align_len(&pool->swork.header_len);
  header = (char *)alloca(pool->swork.header_len);
  snprintf(header, pool->swork.header_len,
    "%s%s%s%s%s%s%s%s",
    pool->swork.bbversion,
    pool->swork.prev_hash,
    blank_merkel,
    pool->swork.ntime,
    pool->swork.nbit,
    "00000000", /* nonce */
    roots ? roots : "",
    pool->algorithm.type == ALGO_LYRA2ZZ ? workpadding_l2zz : workpadding);
  // header_bin size is a padded multiple of 64-bytes
  if (unlikely(!hex2bin(pool->header_bin, header, (roots && pool->algorithm.type == ALGO_LYRA2ZZ) ? 128 : (roots ? 192 : 128)))) {
    applog(LOG_WARNING, "%s: Failed to convert header to header_bin, got %d, %s, %s", __func__, pool->algorithm.type == ALGO_LYRA2ZZ, roots, header);
    pool_failed(pool);
    // TODO: memory leaks? goto out, clean up there?
    return false;
  }

  cb1 = (unsigned char *)calloc(cb1_len, 1);
  if (unlikely(!cb1))
    quithere(1, "Failed to calloc cb1 in parse_notify");
  hex2bin(cb1, coinbase1, cb1_len);

  cb2 = (unsigned char *)calloc(cb2_len, 1);
  if (unlikely(!cb2))
    quithere(1, "Failed to calloc cb2 in parse_notify");
  hex2bin(cb2, coinbase2, cb2_len);

  free(pool->coinbase);
  align_len(&alloc_len);
  pool->coinbase = (unsigned char *)calloc(alloc_len, 1);
  if (unlikely(!pool->coinbase))
    quit(1, "Failed to calloc pool coinbase in parse_notify");
  memcpy(pool->coinbase, cb1, cb1_len);
  memcpy(pool->coinbase + cb1_len, pool->nonce1bin, pool->n1_len);
  // NOTE: gap for nonce2, filled at work generation time
  memcpy(pool->coinbase + cb1_len + pool->n1_len + pool->n2size, cb2, cb2_len);
  cg_wunlock(&pool->data_lock);

  if (opt_protocol) {
    applog(LOG_DEBUG, "job_id: %s", job_id);
    applog(LOG_DEBUG, "prev_hash: %s", prev_hash);
    applog(LOG_DEBUG, "coinbase1: %s", coinbase1);
    applog(LOG_DEBUG, "coinbase2: %s", coinbase2);
    applog(LOG_DEBUG, "bbversion: %s", bbversion);
    applog(LOG_DEBUG, "nbit: %s", nbit);
    applog(LOG_DEBUG, "ntime: %s", ntime);
    applog(LOG_DEBUG, "clean: %s", clean ? "yes" : "no");
  }
  free(coinbase1);
  free(coinbase2);
  free(cb1);
  free(cb2);

  /* A notify message is the closest stratum gets to a getwork */
  pool->getwork_requested++;
  total_getworks++;
  ret = true;
  if (pool == current_pool())
    opt_work_update = true;
out:
  return ret;
}

extern double le256todiff(const void *le256, double diff_multiplier);

static bool parse_notify_ethash(struct pool *pool, json_t *val)
{
  char *job_id;
  bool clean;
  uint8_t EthWork[32], SeedHash[32], Target[32], NetDiff[32];
  char *EthWorkStr, *SeedHashStr, *TgtStr, *NetDiffStr = NULL;
  int ret = true;

  job_id = json_array_string(val, 0);
  EthWorkStr = json_array_string(val, 1);
  SeedHashStr = json_array_string(val, 2);
  TgtStr = json_array_string(val, 3);
  clean = json_is_true(json_array_get(val, 4));
  
  if(json_array_size(val) == 6) {
    applog(LOG_DEBUG, "Pool supports network target.");
    NetDiffStr = json_array_string(val, 5);
  }

  if (job_id == NULL || SeedHashStr == NULL || EthWorkStr == NULL || TgtStr == NULL) {
    applog(LOG_DEBUG, "parse_notify_ethash: Missing an array value.");
    ret = false;
    goto out;
  }
  
  ret &= eth_hex2bin(EthWork, EthWorkStr, 32);
  
  ret &= eth_hex2bin(SeedHash, SeedHashStr, 32);
  
  ret &= eth_hex2bin(Target, TgtStr, 32);
  
  if (!ret || (NetDiffStr != NULL && !eth_hex2bin(NetDiff, NetDiffStr, 32))) {
    ret = false;
    goto out;
  }
  
  cg_wlock(&pool->data_lock);
  
  free(pool->swork.job_id);
  pool->swork.job_id = strdup(job_id);
  pool->swork.clean = clean;
 
  if (memcmp(pool->eth_cache.seed_hash, SeedHash, 32)) {
    pool->eth_cache.current_epoch = EthCalcEpochNumber(SeedHash);
    memcpy(pool->eth_cache.seed_hash, SeedHash, 32);
    eth_gen_cache(pool);
  }
  memcpy(pool->EthWork, EthWork, 32);
  
  swab256(pool->Target, Target);
  pool->swork.diff = le256todiff(pool->Target, pool->algorithm.diff_multiplier2);
  suffix_string_double(pool->swork.diff, pool->diff, sizeof(pool->diff), 0);
  
  pool->diff1 = 0;
  if (NetDiffStr != NULL) {
    swab256(pool->NetDiff, NetDiff);
    pool->diff1 = le256todiff(pool->NetDiff, pool->algorithm.diff_multiplier2);
  }
  pool->getwork_requested++;
  //pool->eth_cache.disabled = false;
  
  cg_wunlock(&pool->data_lock);

  if (opt_protocol) {
    applog(LOG_DEBUG, "job_id: %s", job_id);
    applog(LOG_DEBUG, "EthWork: %s", EthWorkStr);
    applog(LOG_DEBUG, "SeedHash: %s", SeedHashStr);
    applog(LOG_DEBUG, "Target: %s", TgtStr);
    applog(LOG_DEBUG, "clean: %s", clean ? "yes" : "no");
  }

  /* A notify message is the closest stratum gets to a getwork */
  total_getworks++;
  if (pool == current_pool())
    opt_work_update = true;
out:
  /* Annoying but we must not leak memory */
  free(job_id);
  free(SeedHashStr);
  free(EthWorkStr);
  free(TgtStr);
  free(NetDiffStr);
  return ret;
}

static bool parse_diff(struct pool *pool, json_t *val)
{
  double old_diff, diff;

  if (opt_diff_mult == 0.0)
    diff = json_number_value(json_array_get(val, 0)) * pool->algorithm.diff_multiplier1;
  else
    diff = json_number_value(json_array_get(val, 0)) * opt_diff_mult;

  if (diff == 0)
    return false;

  cg_wlock(&pool->data_lock);
  if (pool->next_diff > 0) {
    old_diff = pool->next_diff;
    pool->next_diff = diff;
  } else {
    old_diff = pool->swork.diff;
    pool->next_diff = pool->swork.diff = diff;
  }
  cg_wunlock(&pool->data_lock);

  if (old_diff != diff) {
    int idiff = diff;

    if ((double)idiff == diff) {
      applog(pool == current_pool() ? LOG_NOTICE : LOG_DEBUG, "%s difficulty changed to %d", get_pool_name(pool), idiff);
    }
    else {
      applog(pool == current_pool() ? LOG_NOTICE : LOG_DEBUG, "%s difficulty changed to %.3f", get_pool_name(pool), diff);
    }
  }
  else {
    applog(LOG_DEBUG, "%s difficulty set to %f", get_pool_name(pool), diff);
  }

  return true;
}

static bool parse_target(struct pool *pool, json_t *val)
{
  uint8_t oldtarget[32], target[32], *str;

  if ((str = (uint8_t*)json_array_string(val, 0)) == NULL) {
    applog(LOG_DEBUG, "parse_target: Missing an array value.");
    return false;
  }

  hex2bin(target, (const char*)str, 32);

  cg_wlock(&pool->data_lock);
  memcpy(oldtarget, pool->Target, 32);
if (pool->algorithm.type == ALGO_MTP)
	memcpy(pool->Target,target,32);
else 
  swab256(pool->Target, target);
  cg_wunlock(&pool->data_lock);

  if (memcmp(oldtarget, target, 32) != 0) {
    applog(pool == current_pool() ? LOG_NOTICE : LOG_DEBUG, "%s target changed to %s", get_pool_name(pool), str);
  }

  if (str != NULL) {
    free(str);
  }
 
  return true;
}

static bool parse_extranonce_ethash(struct pool *pool, json_t *val)
{
  char *n1str;

  if (!(n1str = json_array_string(val, 0))) {
    applog(LOG_NOTICE, "extranonce failed");
    return false;
  }
  applog(LOG_NOTICE, "extranonce: %s", n1str);

  cg_wlock(&pool->data_lock);
  free(pool->nonce1);
  pool->nonce1 = n1str;
  pool->n1_len = strlen(n1str) / 2; //size in bytes of nonce1 in the header

  free(pool->nonce1bin);
  if (unlikely(!(pool->nonce1bin = (unsigned char *)calloc(pool->n1_len, 1)))) {
    quithere(1, "%s: Failed to calloc pool->nonce1bin", __func__);
  }

  hex2bin(pool->nonce1bin, pool->nonce1, pool->n1_len);
  pool->n2size = 4 - pool->n1_len; //size in bytes of nonce2 in the header
  pool->nonce2 = 0; //reset nonce 2 to 0
  cg_wunlock(&pool->data_lock);

  applog(LOG_NOTICE, "%s extranonce set to %s", get_pool_name(pool), n1str);

  return true;
}

static bool parse_extranonce(struct pool *pool, json_t *val)
{
  if (pool->algorithm.type == ALGO_ETHASH) {
    return parse_extranonce_ethash(pool, val);
  }

  char *nonce1;
  int n2size;

  nonce1 = json_array_string(val, 0);
  if (!nonce1) {
    return false;
  }
  n2size = json_integer_value(json_array_get(val, 1));
  if (!n2size) {
    free(nonce1);
    return false;
  }

  cg_wlock(&pool->data_lock);
  free(pool->nonce1);
  pool->nonce1 = nonce1;
  pool->n1_len = strlen(nonce1) / 2;
  free(pool->nonce1bin);
  pool->nonce1bin = (unsigned char *)calloc(pool->n1_len, 1);
  if (unlikely(!pool->nonce1bin))
    quithere(1, "Failed to calloc pool->nonce1bin");
  hex2bin(pool->nonce1bin, pool->nonce1, pool->n1_len);
  pool->n2size = n2size;
  cg_wunlock(&pool->data_lock);

  applog(LOG_NOTICE, "%s extranonce change requested", get_pool_name(pool));

  return true;
}

static void __suspend_stratum(struct pool *pool)
{
  clear_sockbuf(pool);
  pool->stratum_active = pool->stratum_notify = false;
  if (pool->sock)
    CLOSESOCKET(pool->sock);
  pool->sock = 0;
}

static bool parse_reconnect(struct pool *pool, json_t *val)
{
  if (opt_disable_client_reconnect) {
    applog(LOG_WARNING, "Stratum client.reconnect received but is disabled, not reconnecting.");
    return false;
  }

  char *url, *port, address[256];
  char *sockaddr_url, *stratum_port, *tmp; /* Tempvars. */

  url = (char *)json_string_value(json_array_get(val, 0));
  if (!url)
    url = pool->sockaddr_url;

  port = (char *)json_string_value(json_array_get(val, 1));
  if (!port)
    port = pool->stratum_port;

  snprintf(address, sizeof(address), "%s:%s", url, port);
  if (!extract_sockaddr(address, &sockaddr_url, &stratum_port))
    return false;

  applog(LOG_NOTICE, "Reconnect requested from %s to %s", get_pool_name(pool), address);

  clear_pool_work(pool);

  mutex_lock(&pool->stratum_lock);
  __suspend_stratum(pool);
  tmp = pool->sockaddr_url;
  pool->sockaddr_url = sockaddr_url;
  pool->stratum_url = pool->sockaddr_url;
  free(tmp);
  tmp = pool->stratum_port;
  pool->stratum_port = stratum_port;
  free(tmp);
  mutex_unlock(&pool->stratum_lock);

  if (!restart_stratum(pool)) {
    pool_failed(pool);
    return false;
  }

  return true;
}

static bool send_version(struct pool *pool, json_t *val)
{
  char s[RBUFSIZE];
  int id = json_integer_value(json_object_get(val, "id"));

  if (!id)
    return false;

  sprintf(s, "{\"id\": %d, \"result\": \""PACKAGE"/"CGMINER_VERSION"\", \"error\": null}", id);
  if (!stratum_send(pool, s, strlen(s)))
    return false;

  return true;
}

static bool show_message(struct pool *pool, json_t *val)
{
  char *msg;

  if (!json_is_array(val))
    return false;
  msg = (char *)json_string_value(json_array_get(val, 0));
  if (!msg)
    return false;
  applog(LOG_NOTICE, "%s message: %s", get_pool_name(pool), msg);
  return true;
}

bool parse_method(struct pool *pool, char *s)
{
  json_t *val = NULL, *method, *err_val, *params;
  json_error_t err;
  bool ret = false;
  char *buf;

  if (!s) {
    return ret;
  }

  if (!(val = JSON_LOADS(s, &err))) {
    applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
    return ret;
  }

  if (!(method = json_object_get(val, "method"))) {
    goto done;
  }

  params = json_object_get(val, "params");
  
  // Ethash Stratum sends no error
  if(pool->algorithm.type != ALGO_ETHASH)
  {
    err_val = json_object_get(val, "error");

    if (err_val && !json_is_null(err_val)) {
    char *ss;

    if (err_val) {
      ss = json_dumps(err_val, JSON_INDENT(3));
    }
    else {
      ss = strdup("(unknown reason)");
    }

    applog(LOG_INFO, "JSON-RPC method decode failed: %s", ss);

    free(ss);
    goto done;
    }
  }

  buf = (char *)json_string_value(method);
  if (!buf) {
    goto done;
  }

  if (!strncasecmp(buf, "mining.notify", 13)) {
    if (pool->algorithm.type == ALGO_ETHASH) {
      ret = parse_notify_ethash(pool, params);
    }
    else {
      ret = parse_notify(pool, params);
    }
    
    pool->stratum_notify = ret;
    goto done;
  }

  if (!strncasecmp(buf, "mining.set_difficulty", 21) && parse_diff(pool, params)) {
    ret = true;
    goto done;
  }

  if (!strncasecmp(buf, "mining.set_extranonce", 21) && parse_extranonce(pool, params)) {
    ret = true;
    goto done;
  }

  if (!strncasecmp(buf, "client.reconnect", 16) && parse_reconnect(pool, params)) {
    ret = true;
    goto done;
  }

  if (!strncasecmp(buf, "client.get_version", 18) && send_version(pool, val)) {
    ret = true;
    goto done;
  }

  if (!strncasecmp(buf, "client.show_message", 19) && show_message(pool, params)) {
    ret = true;
    goto done;
  }

  if (!strncasecmp(buf, "mining.set_target", 17) && parse_target(pool, params)) {
    ret = true;
    goto done;
  }

done:
  json_decref(val); // TBD???
  return ret;
}

bool parse_method_bos(struct pool *pool, json_t *val)
{


	json_t  *method, *err_val, *params;
	json_error_t err;
	bool ret = false;
	char *buf;
/*
	if (!s) {
		return ret;
	}
*/
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d)");
		return ret;
	}

	if (!(method = json_object_get(val, "method"))) {
		goto done;
	}

	params = json_object_get(val, "params");

	// Ethash Stratum sends no error
	if (pool->algorithm.type != ALGO_ETHASH)
	{
		err_val = json_object_get(val, "error");

		if (err_val && !json_is_null(err_val)) {
			char *ss;

			if (err_val) {
				ss = json_dumps(err_val, JSON_INDENT(3));
			}
			else {
				ss = strdup("(unknown reason)");
			}

			applog(LOG_INFO, "JSON-RPC method decode failed: %s", ss);

			free(ss);
			goto done;
		}
	}

	buf = (char *)json_string_value(method);
	if (!buf) {
		goto done;
	}

	applog(LOG_DEBUG, "We made it to parse_method()!");

	if (!strncasecmp(buf, "mining.notify", 13)) {

			ret = parse_notify(pool, params);
		

		pool->stratum_notify = ret;
		goto done;
	}

	//cryptonight uses the "job" method instead of mining.notify


	if (!strncasecmp(buf, "mining.set_difficulty", 21) && parse_diff(pool, params)) {
		ret = true;
		goto done;
	}

	if (!strncasecmp(buf, "mining.set_extranonce", 21) && parse_extranonce(pool, params)) {
		ret = true;
		goto done;
	}

	if (!strncasecmp(buf, "client.reconnect", 16) && parse_reconnect(pool, params)) {
		ret = true;
		goto done;
	}

	if (!strncasecmp(buf, "client.get_version", 18) && send_version(pool, val)) {
		ret = true;
		goto done;
	}

	if (!strncasecmp(buf, "client.show_message", 19) && show_message(pool, params)) {
		ret = true;
		goto done;
	}

	if (!strncasecmp(buf, "mining.set_target", 17) && parse_target(pool, params)) {
		ret = true;
		goto done;
	}

done:
	json_decref(val);
	return ret;
}


bool subscribe_extranonce(struct pool *pool)
{
  json_t *val = NULL, *res_val, *err_val;
  char s[RBUFSIZE], *sret = NULL;
  json_error_t err;
  bool ret = false;

  sprintf(s, "{\"id\": %d, \"method\": \"mining.extranonce.subscribe\", \"params\": []}", swork_id++);

  if (!stratum_send(pool, s, strlen(s))) {
    return ret;
  }

  /* Parse all data in the queue and anything left should be the response */
  while (42) {
    if (!socket_full(pool, DEFAULT_SOCKWAIT / 30)) {
      applog(LOG_DEBUG, "Timed out waiting for response extranonce.subscribe");
      /* some pool doesnt send anything, so this is normal */
      ret = true;
      goto out;
    }

    sret = recv_line(pool);
    if (!sret) {
      return ret;
    }
    else if (parse_method(pool, sret)) {
      free(sret);
    }
    else {
      break;
    }
  }

  val = JSON_LOADS(sret, &err);
  free(sret);
  res_val = json_object_get(val, "result");
  err_val = json_object_get(val, "error");

  if (!res_val || json_is_false(res_val) || (err_val && !json_is_null(err_val) && !json_is_false(err_val)))  {
    char *ss;

    if (err_val) {
      ss = __json_array_string(err_val, 1);
      if (!ss)
        ss = (char *)json_string_value(err_val);
      if (ss && (strcmp(ss, "Method 'subscribe' not found for service 'mining.extranonce'") == 0)) {
        applog(LOG_INFO, "Cannot subscribe to mining.extranonce on %s", get_pool_name(pool));
        ret = true;
        goto out;
      }
      if (ss && (strcmp(ss, "Unrecognized request provided") == 0)) {
        applog(LOG_INFO, "Cannot subscribe to mining.extranonce on %s", get_pool_name(pool));
        ret = true;
        goto out;
      }
      ss = json_dumps(err_val, JSON_INDENT(3));
    }
    else
      ss = strdup("(unknown reason)");
    applog(LOG_INFO, "%s JSON stratum auth failed: %s", get_pool_name(pool), ss);
    free(ss);

    goto out;
  }

  ret = true;
  applog(LOG_INFO, "Stratum extranonce subscribe for %s", get_pool_name(pool));

out:
  json_decref(val);
  return ret;
}

bool auth_stratum(struct pool *pool)
{
  json_t *val = NULL, *res_val, *err_val;
  char s[RBUFSIZE], *sret = NULL;
  json_error_t err;
  bool ret = false;

  sprintf(s, "{\"id\": %d, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}",
    swork_id++, pool->rpc_user, pool->rpc_pass);

  if (!stratum_send(pool, s, strlen(s))) {
    return ret;
  }

  /* Parse all data in the queue and anything left should be auth */
  while (42) {
    sret = recv_line(pool);

    if (!sret) {
      return ret;
    }
    else if (parse_method(pool, sret)) {
      free(sret);
    }
    else {
      break;
    }
  }

  val = JSON_LOADS(sret, &err);
  free(sret);
  res_val = json_object_get(val, "result");
  err_val = json_object_get(val, "error");

  if (!res_val || json_is_false(res_val) || (err_val && !json_is_null(err_val)))  {
    char *ss;

    if (err_val)
      ss = json_dumps(err_val, JSON_INDENT(3));
    else
      ss = strdup("(unknown reason)");
    applog(LOG_INFO, "%s JSON stratum auth failed: %s", get_pool_name(pool), ss);
    free(ss);

    suspend_stratum(pool);

    goto out;
  }

  ret = true;
  applog(LOG_INFO, "Stratum authorisation success for %s", get_pool_name(pool));
  pool->probed = true;
  successful_connect = true;

out:
  json_decref(val);
  return ret;
}

bool auth_stratum_bos(struct pool *pool)
{

	json_t *val = NULL, *res_val, *err_val, *res_id, *res_job;
	char s[RBUFSIZE], *sret = NULL;
	json_error_t err;
	bool ret = false;

	json_t *MyObject = json_object();
	json_t *json_arr = json_array();
	json_object_set_new(MyObject, "id", json_integer(swork_id++));
	json_object_set_new(MyObject, "method", json_string("mining.authorize"));
	json_object_set_new(MyObject, "params", json_arr);
	json_array_append_new(json_arr, json_string(pool->rpc_user));
	json_array_append_new(json_arr, json_string(pool->rpc_pass));

	json_error_t boserror;
	bos_t *serialized = bos_serialize(MyObject, &boserror);
	
  json_decref(MyObject);
	if (!stratum_send_bos(pool, (char*)serialized->data, serialized->size)) {
    bos_free(serialized);
    return ret;
  }

	/* Parse all data in the queue and anything left should be auth */
	while (42) {
		sret = recv_line_bos(pool);

		if (!sret) {
      bos_free(serialized);
			return ret;
		}
		else if (parse_method(pool, sret)) {
			free(sret);
		}
		else {
			break;
		}
	}

	val = JSON_LOADS(sret, &err);
	free(sret);
	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val || json_is_false(res_val) || (err_val && !json_is_null(err_val))) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");
		applog(LOG_INFO, "%s JSON stratum auth failed: %s", get_pool_name(pool), ss);
		free(ss);

		suspend_stratum(pool);

		goto out;
	}

	//check if the result contains an id... if so then we need to process as first job


	ret = true;
	applog(LOG_INFO, "Stratum authorisation success for %s", get_pool_name(pool));
	pool->probed = true;
	successful_connect = true;

out:
	json_decref(val);
  bos_free(serialized);
	return ret;
}

static int recv_byte(int sockd)
{
  char c;

  if (recv(sockd, &c, 1, 0) != -1)
    return c;

  return -1;
}

static bool http_negotiate(struct pool *pool, int sockd, bool http0)
{
  char buf[1024];
  int i, len;

  if (http0) {
    snprintf(buf, 1024, "CONNECT %s:%s HTTP/1.0\r\n\r\n",
      pool->sockaddr_url, pool->stratum_port);
  } else {
    snprintf(buf, 1024, "CONNECT %s:%s HTTP/1.1\r\nHost: %s:%s\r\n\r\n",
      pool->sockaddr_url, pool->stratum_port, pool->sockaddr_url,
      pool->stratum_port);
  }
  applog(LOG_DEBUG, "Sending proxy %s:%s - %s",
    pool->sockaddr_proxy_url, pool->sockaddr_proxy_port, buf);
  send(sockd, buf, strlen(buf), 0);
  len = recv(sockd, buf, 12, 0);
  if (len <= 0) {
    applog(LOG_WARNING, "Couldn't read from proxy %s:%s after sending CONNECT",
           pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
    return false;
  }
  buf[len] = '\0';
  applog(LOG_DEBUG, "Received from proxy %s:%s - %s",
         pool->sockaddr_proxy_url, pool->sockaddr_proxy_port, buf);
  if (strcmp(buf, "HTTP/1.1 200") && strcmp(buf, "HTTP/1.0 200")) {
    applog(LOG_WARNING, "HTTP Error from proxy %s:%s - %s",
           pool->sockaddr_proxy_url, pool->sockaddr_proxy_port, buf);
    return false;
  }

  /* Ignore unwanted headers till we get desired response */
  for (i = 0; i < 4; i++) {
    buf[i] = recv_byte(sockd);
    if (buf[i] == (char)-1) {
      applog(LOG_WARNING, "Couldn't read HTTP byte from proxy %s:%s",
      pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
      return false;
    }
  }
  while (strncmp(buf, "\r\n\r\n", 4)) {
    for (i = 0; i < 3; i++)
      buf[i] = buf[i + 1];
    buf[3] = recv_byte(sockd);
    if (buf[3] == (char)-1) {
      applog(LOG_WARNING, "Couldn't read HTTP byte from proxy %s:%s",
      pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
      return false;
    }
  }

  applog(LOG_DEBUG, "Success negotiating with %s:%s HTTP proxy",
         pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
  return true;
}

static bool socks5_negotiate(struct pool *pool, int sockd)
{
  unsigned char atyp, uclen;
  unsigned short port;
  char buf[515];
  int i, len;

  buf[0] = 0x05;
  buf[1] = 0x01;
  buf[2] = 0x00;
  applog(LOG_DEBUG, "Attempting to negotiate with %s:%s SOCKS5 proxy",
         pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
  send(sockd, buf, 3, 0);
  if (recv_byte(sockd) != 0x05 || recv_byte(sockd) != buf[2]) {
    applog(LOG_WARNING, "Bad response from %s:%s SOCKS5 server",
           pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
    return false;
  }

  buf[0] = 0x05;
  buf[1] = 0x01;
  buf[2] = 0x00;
  buf[3] = 0x03;
  len = (strlen(pool->sockaddr_url));
  if (len > 255)
    len = 255;
  uclen = len;
  buf[4] = (uclen & 0xff);
  memcpy(buf + 5, pool->sockaddr_url, len);
  port = atoi(pool->stratum_port);
  buf[5 + len] = (port >> 8);
  buf[6 + len] = (port & 0xff);
  send(sockd, buf, (7 + len), 0);
  if (recv_byte(sockd) != 0x05 || recv_byte(sockd) != 0x00) {
    applog(LOG_WARNING, "Bad response from %s:%s SOCKS5 server",
      pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
    return false;
  }

  recv_byte(sockd);
  atyp = recv_byte(sockd);
  if (atyp == 0x01) {
    for (i = 0; i < 4; i++)
      recv_byte(sockd);
  } else if (atyp == 0x03) {
    len = recv_byte(sockd);
    for (i = 0; i < len; i++)
      recv_byte(sockd);
  } else {
    applog(LOG_WARNING, "Bad response from %s:%s SOCKS5 server",
      pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
    return false;
  }
  for (i = 0; i < 2; i++)
    recv_byte(sockd);

  applog(LOG_DEBUG, "Success negotiating with %s:%s SOCKS5 proxy",
         pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
  return true;
}

static bool socks4_negotiate(struct pool *pool, int sockd, bool socks4a)
{
  unsigned short port;
  in_addr_t inp;
  char buf[515];
  int i, len;
  int ret;

  buf[0] = 0x04;
  buf[1] = 0x01;
  port = atoi(pool->stratum_port);
  buf[2] = port >> 8;
  buf[3] = port & 0xff;
  sprintf(&buf[8], "SGMINER");

  /* See if we've been given an IP address directly to avoid needing to
   * resolve it. */
  inp = inet_addr(pool->sockaddr_url);
  inp = ntohl(inp);
  if ((int)inp != -1)
    socks4a = false;
  else {
    /* Try to extract the IP address ourselves first */
    struct addrinfo servinfobase, *servinfo, hints;

    servinfo = &servinfobase;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; /* IPV4 only */
    ret = getaddrinfo(pool->sockaddr_url, NULL, &hints, &servinfo);
    if (!ret) {
      applog(LOG_ERR, "getaddrinfo() in socks4_negotiate() returned %i: %s", ret, gai_strerror(ret));

      struct sockaddr_in *saddr_in = (struct sockaddr_in *)servinfo->ai_addr;

      inp = ntohl(saddr_in->sin_addr.s_addr);
      socks4a = false;
      freeaddrinfo(servinfo);
    }
  }

  if (!socks4a) {
    if ((int)inp == -1) {
      applog(LOG_WARNING, "Invalid IP address specified for socks4 proxy: %s",
             pool->sockaddr_url);
      return false;
    }
    buf[4] = (inp >> 24) & 0xFF;
    buf[5] = (inp >> 16) & 0xFF;
    buf[6] = (inp >>  8) & 0xFF;
    buf[7] = (inp >>  0) & 0xFF;
    send(sockd, buf, 16, 0);
  } else {
    /* This appears to not be working but hopefully most will be
     * able to resolve IP addresses themselves. */
    buf[4] = 0;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 1;
    len = strlen(pool->sockaddr_url);
    if (len > 255)
      len = 255;
    memcpy(&buf[16], pool->sockaddr_url, len);
    len += 16;
    buf[len++] = '\0';
    send(sockd, buf, len, 0);
  }

  if (recv_byte(sockd) != 0x00 || recv_byte(sockd) != 0x5a) {
    applog(LOG_WARNING, "Bad response from %s:%s SOCKS4 server",
           pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
    return false;
  }

  for (i = 0; i < 6; i++)
    recv_byte(sockd);

  return true;
}

static void noblock_socket(SOCKETTYPE fd)
{
#ifndef WIN32
  int flags = fcntl(fd, F_GETFL, 0);

  fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
  u_long flags = 1;

  ioctlsocket(fd, FIONBIO, &flags);
#endif
}

static void block_socket(SOCKETTYPE fd)
{
#ifndef WIN32
  int flags = fcntl(fd, F_GETFL, 0);

  fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#else
  u_long flags = 0;

  ioctlsocket(fd, FIONBIO, &flags);
#endif
}

static bool sock_connecting(void)
{
#ifndef WIN32
  return errno == EINPROGRESS;
#else
  return WSAGetLastError() == WSAEWOULDBLOCK;
#endif
}
static bool setup_stratum_socket(struct pool *pool)
{
  struct addrinfo servinfobase, *servinfo, *hints, *p;
  char *sockaddr_url, *sockaddr_port;
  int sockd;
  int ret;

  mutex_lock(&pool->stratum_lock);
  pool->stratum_active = false;
  if (pool->sock) {
    /* FIXME: change to LOG_DEBUG if issue #88 resolved */
    applog(LOG_INFO, "Closing %s socket", get_pool_name(pool));
    CLOSESOCKET(pool->sock);
  }
  pool->sock = 0;
  mutex_unlock(&pool->stratum_lock);

  hints = &pool->stratum_hints;
  memset(hints, 0, sizeof(struct addrinfo));
  hints->ai_family = AF_UNSPEC;
  hints->ai_socktype = SOCK_STREAM;
  servinfo = &servinfobase;

  if (!pool->rpc_proxy && opt_socks_proxy) {
    pool->rpc_proxy = opt_socks_proxy;
    extract_sockaddr(pool->rpc_proxy, &pool->sockaddr_proxy_url, &pool->sockaddr_proxy_port);
    pool->rpc_proxytype = PROXY_SOCKS5;
  }

  if (pool->rpc_proxy) {
    sockaddr_url = pool->sockaddr_proxy_url;
    sockaddr_port = pool->sockaddr_proxy_port;
  } else {
    sockaddr_url = pool->sockaddr_url;
    sockaddr_port = pool->stratum_port;
  }

  ret = getaddrinfo(sockaddr_url, sockaddr_port, hints, &servinfo);
  if (ret) {
    applog(LOG_INFO, "getaddrinfo() in setup_stratum_socket() returned %i: %s", ret, gai_strerror(ret));
    if (!pool->probed) {
      applog(LOG_WARNING, "Failed to resolve (wrong URL?) %s:%s",
             sockaddr_url, sockaddr_port);
      pool->probed = true;
    } else {
      applog(LOG_INFO, "Failed to getaddrinfo for %s:%s",
             sockaddr_url, sockaddr_port);
    }
    return false;
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    sockd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockd == -1) {
      applog(LOG_DEBUG, "Failed socket");
      continue;
    }

    /* Iterate non blocking over entries returned by getaddrinfo
     * to cope with round robin DNS entries, finding the first one
     * we can connect to quickly. */
    noblock_socket(sockd);
    if (connect(sockd, p->ai_addr, p->ai_addrlen) == -1) {
      struct timeval tv_timeout = {1, 0};
      int selret;
      fd_set rw;

      if (!sock_connecting()) {
        CLOSESOCKET(sockd);
        applog(LOG_DEBUG, "Failed sock connect");
        continue;
      }
retry:
      FD_ZERO(&rw);
      FD_SET(sockd, &rw);
      selret = select(sockd + 1, NULL, &rw, NULL, &tv_timeout);
      if  (selret > 0 && FD_ISSET(sockd, &rw)) {
        socklen_t len;
        int err, n;

        len = sizeof(err);
        n = getsockopt(sockd, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
        if (!n && !err) {
          applog(LOG_DEBUG, "Succeeded delayed connect");
          block_socket(sockd);
          break;
        }
      }
      if (selret < 0 && interrupted())
        goto retry;
      if (pool->algorithm.type != ALGO_YESCRYPTR16_NAVI) {
        CLOSESOCKET(sockd);
        applog(LOG_DEBUG, "Select timeout/failed connect");
        continue;
      }
    }
    applog(LOG_WARNING, "Succeeded immediate connect");
    block_socket(sockd);

    break;
  }
  if (p == NULL) {
    applog(LOG_INFO, "Failed to connect to stratum on %s:%s",
           sockaddr_url, sockaddr_port);
    freeaddrinfo(servinfo);
    return false;
  }
  freeaddrinfo(servinfo);

  if (pool->rpc_proxy) {
    switch (pool->rpc_proxytype) {
      case PROXY_HTTP_1_0:
        if (!http_negotiate(pool, sockd, true))
          return false;
        break;
      case PROXY_HTTP:
        if (!http_negotiate(pool, sockd, false))
          return false;
        break;
      case PROXY_SOCKS5:
      case PROXY_SOCKS5H:
        if (!socks5_negotiate(pool, sockd))
          return false;
        break;
      case PROXY_SOCKS4:
        if (!socks4_negotiate(pool, sockd, false))
          return false;
        break;
      case PROXY_SOCKS4A:
        if (!socks4_negotiate(pool, sockd, true))
          return false;
        break;
      default:
        applog(LOG_WARNING, "Unsupported proxy type for %s:%s",
               pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
        return false;
        break;
    }
  }

  if (!pool->sockbuf) {
    pool->sockbuf = (char *)calloc(RBUFSIZE, 1);
    if (!pool->sockbuf)
      quithere(1, "Failed to calloc pool sockbuf");
    pool->sockbuf_size = RBUFSIZE;
  }

  pool->sock = sockd;
  keep_sockalive(sockd);
  return true;
}

static char *get_sessionid(json_t *val)
{
  char *ret = NULL;
  json_t *arr_val;
  int arrsize, i;

  arr_val = json_array_get(val, 0);
  if (!arr_val || !json_is_array(arr_val))
    goto out;
  arrsize = json_array_size(arr_val);
  for (i = 0; i < arrsize; i++) {
    json_t *arr = json_array_get(arr_val, i);
    char *notify;

    if (!arr | !json_is_array(arr))
      break;
    notify = __json_array_string(arr, 0);
    if (!notify)
      continue;
    if (!strncasecmp(notify, "mining.notify", 13)) {
      ret = json_array_string(arr, 1);
      break;
    }
  }
out:
  return ret;
}

void suspend_stratum(struct pool *pool)
{
  applog(LOG_INFO, "Closing socket for stratum %s", get_pool_name(pool));

  mutex_lock(&pool->stratum_lock);
  __suspend_stratum(pool);
  mutex_unlock(&pool->stratum_lock);
}

bool initiate_stratum(struct pool *pool)
{
  bool ret = false, recvd = false, noresume = false, sockd = false;
  char s[RBUFSIZE], *sret = NULL, *nonce1, *sessionid;
  json_t *val = NULL, *res_val, *err_val;
  json_error_t err;
  int n2size;

resend:
  if (!setup_stratum_socket(pool)) {
    /* FIXME: change to LOG_DEBUG when issue #88 resolved */
    applog(LOG_INFO, "setup_stratum_socket() on %s failed", get_pool_name(pool));
    sockd = false;
    goto out;
  }

  sockd = true;

  if (recvd) {
    /* Get rid of any crap lying around if we're resending */
    clear_sock(pool);
    sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": []}", swork_id++);
  } else {
    if (pool->sessionid)
      sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\""PACKAGE"/"CGMINER_VERSION"\", \"%s\"]}", swork_id++, pool->sessionid);
    else
      sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\""PACKAGE"/"CGMINER_VERSION"\"]}", swork_id++);
  }

  if (__stratum_send(pool, s, strlen(s)) != SEND_OK) {
    applog(LOG_DEBUG, "Failed to send s in initiate_stratum");
    goto out;
  }

  if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
    applog(LOG_DEBUG, "Timed out waiting for response in initiate_stratum");
    goto out;
  }

  sret = recv_line(pool);
  if (!sret)
    goto out;

  recvd = true;

  val = JSON_LOADS(sret, &err);
  free(sret);
  if (!val) {
    applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
    goto out;
  }

  res_val = json_object_get(val, "result");
  err_val = json_object_get(val, "error");

  if (!res_val || json_is_null(res_val) ||
      (err_val && !json_is_null(err_val))) {
    char *ss;

    if (err_val)
      ss = json_dumps(err_val, JSON_INDENT(3));
    else
      ss = strdup("(unknown reason)");

    applog(LOG_INFO, "Stratum Error: %s", ss);

    free(ss);

    goto out;
  }

  if(pool->algorithm.type == ALGO_ETHASH)
  {
    if(!pool->stratum_url) pool->stratum_url = pool->sockaddr_url;
    
    cg_wlock(&pool->data_lock);
    pool->stratum_active = true;
    pool->next_diff = 0;
    pool->swork.diff = 1;
    
    pool->sessionid = NULL;
    free(pool->nonce1);
    pool->nonce1 = NULL;
    pool->n1_len = 0;
    pool->n2size = 4;
    cg_wunlock(&pool->data_lock);
    
    json_decref(val);
    return true;
  }

  sessionid = get_sessionid(res_val);
  if (!sessionid)
    applog(LOG_DEBUG, "Failed to get sessionid in initiate_stratum");
  nonce1 = json_array_string(res_val, 1);
  if (!nonce1) {
    applog(LOG_INFO, "Failed to get nonce1 in initiate_stratum");
    free(sessionid);
    goto out;
  }
  n2size = json_integer_value(json_array_get(res_val, 2));
  if (n2size < 1)
  {
    applog(LOG_INFO, "Failed to get n2size in initiate_stratum");
    free(sessionid);
    free(nonce1);
    goto out;
  }

  cg_wlock(&pool->data_lock);
  free(pool->nonce1);
  free(pool->sessionid);
  pool->sessionid = sessionid;
  pool->nonce1 = nonce1;
  pool->n1_len = strlen(nonce1) / 2;
  free(pool->nonce1bin);
  pool->nonce1bin = (unsigned char *)calloc(pool->n1_len, 1);
  if (unlikely(!pool->nonce1bin))
    quithere(1, "Failed to calloc pool->nonce1bin");
  hex2bin(pool->nonce1bin, pool->nonce1, pool->n1_len);
  pool->n2size = n2size;
  cg_wunlock(&pool->data_lock);

  if (sessionid)
    applog(LOG_DEBUG, "%s stratum session id: %s", get_pool_name(pool), pool->sessionid);

  ret = true;
out:
  if (ret) {
    if (!pool->stratum_url)
      pool->stratum_url = pool->sockaddr_url;
    pool->stratum_active = true;
    pool->next_diff = 0;
    pool->swork.diff = 1;
    if (opt_protocol) {
      applog(LOG_DEBUG, "%s confirmed mining.subscribe with extranonce1 %s extran2size %d",
             get_pool_name(pool), pool->nonce1, pool->n2size);
    }
  } else {
    if (recvd && !noresume) {
      /* Reset the sessionid used for stratum resuming in case the pool
      * does not support it, or does not know how to respond to the
      * presence of the sessionid parameter. */
      cg_wlock(&pool->data_lock);
      free(pool->sessionid);
      free(pool->nonce1);
      pool->sessionid = pool->nonce1 = NULL;
      cg_wunlock(&pool->data_lock);

      applog(LOG_DEBUG, "Failed to resume stratum, trying afresh");
      noresume = true;
      json_decref(val);
      goto resend;
    }
    applog(LOG_DEBUG, "Initiating stratum failed on %s", get_pool_name(pool));
    if (sockd) {
      applog(LOG_DEBUG, "Suspending stratum on %s", get_pool_name(pool));
      suspend_stratum(pool);
    }
  }

  json_decref(val);
  return ret;
}

bool initiate_stratum_bos(struct pool *pool)
{
#define USER_AGENT PACKAGE_NAME "/" PACKAGE_VERSION

	bool ret = false, recvd = false, noresume = false, sockd = false;
	char s[RBUFSIZE], *sret = NULL, *nonce1, *sessionid;
	json_t *val = NULL, *res_val, *err_val;
	json_error_t err;
	int n2size;
	json_t *MyObject;
	json_t *json_arr;
resend:
	if (!setup_stratum_socket(pool)) {
		/* FIXME: change to LOG_DEBUG when issue #88 resolved */
		applog(LOG_INFO, "setup_stratum_socket() on %s failed", get_pool_name(pool));
		sockd = false;
		goto out;
	}

	sockd = true;
  MyObject = json_object();
	json_arr = json_array();
	json_object_set_new(MyObject, "id", json_integer(swork_id++));
	json_object_set_new(MyObject, "method", json_string("mining.subscribe"));
	json_object_set_new(MyObject, "params", json_arr);
	if (!recvd) {
		json_array_append_new(json_arr, json_string(USER_AGENT));
		if (pool->sessionid)
			json_array_append_new(json_arr, json_string(pool->sessionid));
	} else 
		clear_sock(pool);

	json_error_t boserror;
	bos_t *serialized = bos_serialize(MyObject, &boserror);

  json_decref(MyObject);

	if (__stratum_send_bos(pool, (char*)serialized->data, serialized->size) != SEND_OK) {
	
		applog(LOG_DEBUG, "Failed to send s in initiate_stratum");
		goto out;
	}

	if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
		applog(LOG_DEBUG, "Timed out waiting for response in initiate_stratum");
		goto out;
	}

	sret = recv_line_bos(pool);
	if (!sret)
		goto out;

	recvd = true;

	val = JSON_LOADS(sret, &err);
	free(sret);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val || json_is_null(res_val) ||
		(err_val && !json_is_null(err_val))) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "Stratum Error: %s", ss);

		free(ss);

		goto out;
	}



	sessionid = json_array_string(res_val, 0); //get_sessionid(res_val);
	if (!sessionid) {
		applog(LOG_DEBUG, "Failed to get sessionid in initiate_stratum");
	}

	nonce1 = json_array_string(res_val, 1);
	if (!nonce1) {
		applog(LOG_INFO, "Failed to get nonce1 in initiate_stratum");
		free(sessionid);
		goto out;
	}

	n2size = 8; // by default
	if (n2size < 1)
	{
		applog(LOG_INFO, "Failed to get n2size in initiate_stratum");
		free(sessionid);
		free(nonce1);
		goto out;
	}

	cg_wlock(&pool->data_lock);
	free(pool->nonce1);
	free(pool->sessionid);

	pool->sessionid = sessionid;
	pool->nonce1 = nonce1;
	pool->n1_len = strlen(nonce1) / 2;

	free(pool->nonce1bin);

	pool->nonce1bin = (unsigned char *)calloc(pool->n1_len, 1);
	if (unlikely(!pool->nonce1bin)) {
		quithere(1, "Failed to calloc pool->nonce1bin");
	}

	hex2bin(pool->nonce1bin, pool->nonce1, pool->n1_len);

	pool->n2size = n2size;
	cg_wunlock(&pool->data_lock);

	if (sessionid) {
		applog(LOG_DEBUG, "%s stratum session id: %s", get_pool_name(pool), pool->sessionid);
	}

	ret = true;

out:
	if (ret) {
		if (!pool->stratum_url)
			pool->stratum_url = pool->sockaddr_url;
		pool->stratum_active = true;
		pool->next_diff = 0;
		pool->swork.diff = 1;
		pool->swork.prev_job_id = NULL;
		pool->swork.job_id = NULL;
		if (opt_protocol) {
			applog(LOG_DEBUG, "%s confirmed mining.subscribe with extranonce1 %s extran2size %d",
				get_pool_name(pool), pool->nonce1, pool->n2size);
		}
	}
	else {
		if (recvd && !noresume) {
			/* Reset the sessionid used for stratum resuming in case the pool
			* does not support it, or does not know how to respond to the
			* presence of the sessionid parameter. */
			cg_wlock(&pool->data_lock);
			free(pool->sessionid);
			free(pool->nonce1);
			pool->sessionid = pool->nonce1 = NULL;
			cg_wunlock(&pool->data_lock);

			applog(LOG_DEBUG, "Failed to resume stratum, trying afresh");
			noresume = true;
			json_decref(val);
			goto resend;
		}
		applog(LOG_DEBUG, "Initiating stratum failed on %s", get_pool_name(pool));
		if (sockd) {
			applog(LOG_DEBUG, "Suspending stratum on %s", get_pool_name(pool));
			suspend_stratum(pool);
		}
	}

  bos_free(serialized);
	json_decref(val);
	return ret;
}

bool restart_stratum(struct pool *pool)
{
  applog(LOG_DEBUG, "Restarting stratum on pool %s", get_pool_name(pool));

if (pool->algorithm.type == ALGO_MTP) {
  if (pool->stratum_active)
		suspend_stratum(pool);
	if (!initiate_stratum_bos(pool))
		return false;
	if (!auth_stratum_bos(pool))
		return false;
} else {
    if (pool->stratum_active)
    suspend_stratum(pool);
  if (!initiate_stratum(pool))
    return false;
  if (pool->extranonce_subscribe && !subscribe_extranonce(pool))
    return false;
  if (!auth_stratum(pool))
    return false;
}

  return true;
}

void dev_error(struct cgpu_info *dev, enum dev_reason reason)
{
  dev->device_last_not_well = time(NULL);
  dev->device_not_well_reason = reason;

  switch (reason) {
    case REASON_THREAD_FAIL_INIT:
      dev->thread_fail_init_count++;
      break;
    case REASON_THREAD_ZERO_HASH:
      dev->thread_zero_hash_count++;
      break;
    case REASON_THREAD_FAIL_QUEUE:
      dev->thread_fail_queue_count++;
      break;
    case REASON_DEV_SICK_IDLE_60:
      dev->dev_sick_idle_60_count++;
      break;
    case REASON_DEV_DEAD_IDLE_600:
      dev->dev_dead_idle_600_count++;
      break;
    case REASON_DEV_NOSTART:
      dev->dev_nostart_count++;
      break;
    case REASON_DEV_OVER_HEAT:
      dev->dev_over_heat_count++;
      break;
    case REASON_DEV_THERMAL_CUTOFF:
      dev->dev_thermal_cutoff_count++;
      break;
    case REASON_DEV_COMMS_ERROR:
      dev->dev_comms_error_count++;
      break;
    case REASON_DEV_THROTTLE:
      dev->dev_throttle_count++;
      break;
  }
}

/* Realloc an existing string to fit an extra string s, appending s to it. */
void *realloc_strcat(char *ptr, char *s)
{
  size_t old = strlen(ptr), len = strlen(s);
  char *ret;

  if (!len)
    return ptr;

  len += old + 1;
  align_len(&len);

  ret = (char *)malloc(len);
  if (unlikely(!ret))
    quithere(1, "Failed to malloc");

  sprintf(ret, "%s%s", ptr, s);
  free(ptr);
  return ret;
}

void RenameThread(const char* name)
{
  char buf[16];

  snprintf(buf, sizeof(buf), "cg@%s", name);
#if defined(PR_SET_NAME)
  // Only the first 15 characters are used (16 - NUL terminator)
  prctl(PR_SET_NAME, buf, 0, 0, 0);
#elif (defined(__FreeBSD__) || defined(__OpenBSD__))
  pthread_set_name_np(pthread_self(), buf);
#elif defined(MAC_OSX)
  pthread_setname_np(buf);
#else
  // Prevent warnings
  (void)buf;
#endif
}

/* sgminer specific wrappers for true unnamed semaphore usage on platforms
 * that support them and for apple which does not. We use a single byte across
 * a pipe to emulate semaphore behaviour there. */
#ifdef __APPLE__
void _cgsem_init(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
  int flags, fd, i;

  if (pipe(cgsem->pipefd) == -1)
    quitfrom(1, file, func, line, "Failed pipe errno=%d", errno);

  /* Make the pipes FD_CLOEXEC to allow them to close should we call
   * execv on restart. */
  for (i = 0; i < 2; i++) {
    fd = cgsem->pipefd[i];
    flags = fcntl(fd, F_GETFD, 0);
    flags |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, flags) == -1)
      quitfrom(1, file, func, line, "Failed to fcntl errno=%d", errno);
  }
}

void _cgsem_post(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
  const char buf = 1;
  int ret;

retry:
  ret = write(cgsem->pipefd[1], &buf, 1);
  if (unlikely(ret == 0))
    applog(LOG_WARNING, "Failed to write errno=%d" IN_FMT_FFL, errno, file, func, line);
  else if (unlikely(ret < 0 && interrupted))
    goto retry;
}

void _cgsem_wait(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
  char buf;
  int ret;
retry:
  ret = read(cgsem->pipefd[0], &buf, 1);
  if (unlikely(ret == 0))
    applog(LOG_WARNING, "Failed to read errno=%d" IN_FMT_FFL, errno, file, func, line);
  else if (unlikely(ret < 0 && interrupted))
    goto retry;
}

void cgsem_destroy(cgsem_t *cgsem)
{
  close(cgsem->pipefd[1]);
  close(cgsem->pipefd[0]);
}

/* This is similar to sem_timedwait but takes a millisecond value */
int _cgsem_mswait(cgsem_t *cgsem, int ms, const char *file, const char *func, const int line)
{
  struct timeval timeout;
  int ret, fd;
  fd_set rd;
  char buf;

retry:
  fd = cgsem->pipefd[0];
  FD_ZERO(&rd);
  FD_SET(fd, &rd);
  ms_to_timeval(&timeout, ms);
  ret = select(fd + 1, &rd, NULL, NULL, &timeout);

  if (ret > 0) {
    ret = read(fd, &buf, 1);
    return 0;
  }
  if (likely(!ret))
    return ETIMEDOUT;
  if (interrupted())
    goto retry;
  quitfrom(1, file, func, line, "Failed to sem_timedwait errno=%d cgsem=0x%p", errno, cgsem);
  /* We don't reach here */
  return 0;
}

/* Reset semaphore count back to zero */
void cgsem_reset(cgsem_t *cgsem)
{
  int ret, fd;
  fd_set rd;
  char buf;

  fd = cgsem->pipefd[0];
  FD_ZERO(&rd);
  FD_SET(fd, &rd);
  do {
    struct timeval timeout = {0, 0};

    ret = select(fd + 1, &rd, NULL, NULL, &timeout);
    if (ret > 0)
      ret = read(fd, &buf, 1);
    else if (unlikely(ret < 0 && interrupted()))
      ret = 1;
  } while (ret > 0);
}
#else
void _cgsem_init(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
  int ret;
  if ((ret = sem_init(cgsem, 0, 0)))
    quitfrom(1, file, func, line, "Failed to sem_init ret=%d errno=%d", ret, errno);
}

void _cgsem_post(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
  if (unlikely(sem_post(cgsem)))
    quitfrom(1, file, func, line, "Failed to sem_post errno=%d cgsem=0x%p", errno, cgsem);
}

void _cgsem_wait(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
retry:
  if (unlikely(sem_wait(cgsem))) {
    if (interrupted())
      goto retry;
    quitfrom(1, file, func, line, "Failed to sem_wait errno=%d cgsem=0x%p", errno, cgsem);
  }
}

int _cgsem_mswait(cgsem_t *cgsem, int ms, const char *file, const char *func, const int line)
{
  struct timespec abs_timeout, ts_now;
  struct timeval tv_now;
  int ret;

  cgtime(&tv_now);
  timeval_to_spec(&ts_now, &tv_now);
  ms_to_timespec(&abs_timeout, ms);
retry:
  timeraddspec(&abs_timeout, &ts_now);
  ret = sem_timedwait(cgsem, &abs_timeout);

  if (ret) {
    if (likely(sock_timeout()))
      return ETIMEDOUT;
    if (interrupted())
      goto retry;
    quitfrom(1, file, func, line, "Failed to sem_timedwait errno=%d cgsem=0x%p", errno, cgsem);
  }
  return 0;
}

void cgsem_reset(cgsem_t *cgsem)
{
  int ret;

  do {
    ret = sem_trywait(cgsem);
    if (unlikely(ret < 0 && interrupted()))
      ret = 0;
  } while (!ret);
}

void cgsem_destroy(cgsem_t *cgsem)
{
  sem_destroy(cgsem);
}
#endif

/* Provide a completion_timeout helper function for unreliable functions that
 * may die due to driver issues etc that time out if the function fails and
 * can then reliably return. */
struct cg_completion {
  cgsem_t cgsem;
  void (*fn)(void *fnarg);
  void *fnarg;
};

void *completion_thread(void *arg)
{
  struct cg_completion *cgc = (struct cg_completion *)arg;

  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  cgc->fn(cgc->fnarg);
  cgsem_post(&cgc->cgsem);

  return NULL;
}

bool cg_completion_timeout(void *fn, void *fnarg, int timeout)
{
  struct cg_completion *cgc;
  pthread_t pthread;
  bool ret = false;

  cgc = (struct cg_completion *)malloc(sizeof(struct cg_completion));
  if (unlikely(!cgc))
    return ret;
  cgsem_init(&cgc->cgsem);

#ifdef _MSC_VER
  cgc->fn = (void(__cdecl *)(void *))fn;
#else
  cgc->fn = fn;
#endif

  cgc->fnarg = fnarg;

  pthread_create(&pthread, NULL, completion_thread, (void *)cgc);

  ret = cgsem_mswait(&cgc->cgsem, timeout);

  if (ret)
    pthread_cancel(pthread);

  pthread_join(pthread, NULL);
  free(cgc);
  return !ret;
}
