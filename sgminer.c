/*
 * Copyright 2013-2014 sgminer developers (see AUTHORS.md)
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2011-2012 Luke Dashjr
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#define _CRT_RAND_S
#include "config.h"

#ifdef HAVE_CURSES
#include <curses.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#include <limits.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifndef WIN32
#include <sys/resource.h>
#else
#include <winsock2.h>
#include <windows.h>
#endif
#include <ccan/opt/opt.h>
#include <bosjansson.h>
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#else
char *curly = ":D";
#endif
#include <libgen.h>
#include "sph/sph_sha2.h"
#include "sph/sph_blake.h"

#include "compat.h"
#include "miner.h"
#include "findnonce.h"
#include "adl.h"
#include "driver-opencl.h"
#include "bench_block.h"

#include "algorithm.h"
#include "algorithm/ethash.h"
#include "pool.h"
#include "config_parser.h"
#include "events.h"

#if defined(unix) || defined(__APPLE__)
  #include <errno.h>
  #include <fcntl.h>
  #include <sys/wait.h>
#endif


static char packagename[256];

static bool startup = true; //sgminer is starting up
static bool gpu_initialized = false;  //gpu initialized
static int init_pool; //pool used to initialize gpus
static bool on_backup_pool = false; //for simple connect strategy... flag if we're on a backup pool

bool opt_work_update;
bool opt_protocol;
bool have_longpoll;
bool want_per_device_stats;
bool use_syslog;
bool opt_quiet;
bool opt_realquiet;
bool opt_loginput;
bool opt_compact;
bool opt_incognito;

// remote config options...
int opt_remoteconf_retry = 3; // number of retries
int opt_remoteconf_wait = 10; // wait in secs between retries
bool opt_remoteconf_usecache = false; // use last downloaded copy of the config file when download fails

int opt_cutofftemp = 95;
int opt_log_interval = 5;
int opt_queue = 1;
int opt_scantime = 7;
int opt_expiry = 28;

unsigned long long global_hashrate;
unsigned long global_quota_gcd = 1;
bool opt_show_coindiff = false;
time_t last_getwork;

int nDevs;
int opt_dynamic_interval = 7;
int opt_g_threads = -1;
bool opt_restart = true;
int opt_vote = 0;

/*****************************************
 * Xn Algorithm options
 *****************************************/
int opt_hamsi_expand_big = 4;
int opt_keccak_unroll = 0;
bool opt_hamsi_short = false;
bool opt_blake_compact = false;
bool opt_luffa_parallel = false;

struct list_head scan_devices;
bool devices_enabled[MAX_DEVICES];
int opt_devs_enabled;
static bool opt_display_devs;
bool opt_removedisabled;
int total_devices;
static int most_devices;
struct cgpu_info **devices;
int mining_threads;

#ifdef HAVE_CURSES
bool use_curses = true;
#else
bool use_curses;
#endif

static bool opt_submit_stale = true;
int opt_shares;
bool opt_fail_only;
int opt_fail_switch_delay = 60;
int opt_watchpool_refresh = 30;
static bool opt_fix_protocol;
static bool opt_lowmem;
static bool opt_morenotices;
uint8_t entropy[32];
uint32_t eth_nonce;
pthread_mutex_t eth_nonce_lock;
bool opt_autofan;
bool opt_autoengine;
bool opt_noadl;
char *opt_api_allow = NULL;
char *opt_api_groups;
char *opt_api_description = PACKAGE_STRING;
int opt_api_port = 4028;
bool opt_api_listen;
bool opt_api_mcast;
char *opt_api_mcast_addr = API_MCAST_ADDR;
char *opt_api_mcast_code = API_MCAST_CODE;
char *opt_api_mcast_des = "";
int opt_api_mcast_port = 4028;
bool opt_api_network;
bool opt_delaynet;
bool opt_disable_pool;
bool opt_disable_client_reconnect = false;
static bool no_work;
bool opt_worktime;
#if defined(HAVE_LIBCURL) && defined(CURL_HAS_KEEPALIVE)
int opt_tcp_keepalive = 30;
#else
int opt_tcp_keepalive;
#endif
double opt_diff_mult = 0.0;

char *opt_kernel_path;
char *sgminer_path;

#define QUIET (opt_quiet || opt_realquiet)

struct thr_info *control_thr;
struct thr_info **mining_thr = NULL;
static int gwsched_thr_id;
static int watchpool_thr_id;
static int watchdog_thr_id;
#ifdef HAVE_CURSES
static int input_thr_id;
#endif
int gpur_thr_id;
static int api_thr_id;
static int total_control_threads;

#if LOCK_TRACKING
pthread_mutex_t lockstat_lock;
#endif

pthread_mutex_t hash_lock;
static pthread_mutex_t *stgd_lock;
pthread_mutex_t console_lock;
cglock_t ch_lock;
static pthread_rwlock_t blk_lock;
static pthread_mutex_t sshare_lock;

pthread_rwlock_t netacc_lock;
pthread_rwlock_t mining_thr_lock;
pthread_rwlock_t devices_lock;

static pthread_mutex_t lp_lock;
static pthread_cond_t lp_cond;

static pthread_mutex_t algo_switch_lock;
static int algo_switch_n = 0;
static unsigned long pool_switch_options = 0;
static pthread_mutex_t algo_switch_wait_lock;
static pthread_cond_t algo_switch_wait_cond;

pthread_mutex_t restart_lock;
pthread_cond_t restart_cond;

pthread_cond_t gws_cond;

double total_rolling;
double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end, launch_time;

cglock_t control_lock;
pthread_mutex_t stats_lock;

static void *restart_mining_threads_thread(void *userdata);
static void apply_initial_gpu_settings(struct pool *pool);
static unsigned long compare_pool_settings(struct pool *oldpool, struct pool *newpool);
static void apply_switcher_options(unsigned long options, struct pool *pool);
static void restart_mining_threads(unsigned int new_n_threads);
static void probe_pools(void);
static bool test_pool(struct pool *pool);

int hw_errors;
int total_accepted, total_rejected;
double total_diff1;
int total_getworks, total_stale, total_discarded;
double total_diff_accepted, total_diff_rejected, total_diff_stale;
static int staged_rollable;
unsigned int new_blocks;
static unsigned int work_block;
unsigned int found_blocks;

unsigned int local_work;
unsigned int total_go, total_ro;

struct pool **pools;
static struct pool *currentpool = NULL;

struct strategies strategies[] = {
  { "Failover" },
  { "Round Robin" },
  { "Rotate" },
  { "Load Balance" },
  { "Balance" },
};

int total_pools, enabled_pools;
enum pool_strategy pool_strategy = POOL_FAILOVER;
int opt_rotate_period;
static int total_urls;

//default mode apply algorithm/gpu settings when pool changes
int opt_switchmode = SWITCH_POOL;

static
#ifndef HAVE_CURSES
const
#endif
bool curses_active;

/* Protected by ch_lock */
char current_hash[68];
static char prev_block[12];
static char current_block[32];

static char datestamp[40];
static char blocktime[32];
struct timeval block_timeval;
static char best_share[8] = "0";
double current_diff = 0;
static char block_diff[8];
double best_diff = 0;

struct block {
  char hash[68];
  UT_hash_handle hh;
  int block_no;
};

static struct block *blocks = NULL;

int swork_id;

/* For creating a hash database of stratum shares submitted that have not had
 * a response yet */
struct stratum_share {
  UT_hash_handle hh;
  bool block;
  struct work *work;
  int id;
  time_t sshare_time;
  time_t sshare_sent;
};

static struct stratum_share *stratum_shares = NULL;

char *opt_socks_proxy = NULL;

#if defined(unix) || defined(__APPLE__)
  char *opt_stderr_cmd = NULL;
  static int forkpid;
#endif // defined(unix)

#ifndef _MSC_VER
struct sigaction termhandler, inthandler;
#endif

struct thread_q *getq;

static int total_work;
struct work *staged_work = NULL;

struct schedtime schedstart;
struct schedtime schedstop;
bool sched_paused;

static bool time_before(struct tm *tm1, struct tm *tm2)
{
  if (tm1->tm_hour < tm2->tm_hour)
    return true;
  if (tm1->tm_hour == tm2->tm_hour && tm1->tm_min < tm2->tm_min)
    return true;
  return false;
}

static bool should_run(void)
{
  struct timeval tv;
  struct tm *tm;

  if (!schedstart.enable && !schedstop.enable)
    return true;

  cgtime(&tv);
  const time_t tmp_time = tv.tv_sec;
  tm = localtime(&tmp_time);
  if (schedstart.enable) {
    if (!schedstop.enable) {
      if (time_before(tm, &schedstart.tm))
        return false;

      /* This is a once off event with no stop time set */
      schedstart.enable = false;
      return true;
    }
    if (time_before(&schedstart.tm, &schedstop.tm)) {
      if (time_before(tm, &schedstop.tm) && !time_before(tm, &schedstart.tm))
        return true;
      return false;
    } /* Times are reversed */
    if (time_before(tm, &schedstart.tm)) {
      if (time_before(tm, &schedstop.tm))
        return true;
      return false;
    }
    return true;
  }
  /* only schedstop.enable == true */
  if (!time_before(tm, &schedstop.tm))
    return false;
  return true;
}

void get_datestamp(char *f, size_t fsiz, struct timeval *tv)
{
  struct tm *tm;

  const time_t tmp_time = tv->tv_sec;
  tm = localtime(&tmp_time);
  snprintf(f, fsiz, "[%d-%02d-%02d %02d:%02d:%02d]",
    tm->tm_year + 1900,
    tm->tm_mon + 1,
    tm->tm_mday,
    tm->tm_hour,
    tm->tm_min,
    tm->tm_sec);
}

static void get_timestamp(char *f, size_t fsiz, struct timeval *tv)
{
  struct tm *tm;

  const time_t tmp_time = tv->tv_sec;
  tm = localtime(&tmp_time);
  snprintf(f, fsiz, "[%02d:%02d:%02d]",
    tm->tm_hour,
    tm->tm_min,
    tm->tm_sec);
}

static char exit_buf[512];

static void applog_and_exit(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(exit_buf, sizeof(exit_buf), fmt, ap);
  va_end(ap);
  _applog(LOG_ERR, exit_buf, true);
  exit(1);
}

static pthread_mutex_t sharelog_lock;
static FILE *sharelog_file = NULL;

static struct cgpu_info *get_thr_cgpu(int thr_id)
{
  struct cgpu_info *cgpu = NULL;
  rd_lock(&mining_thr_lock);
  if (thr_id < mining_threads && mining_thr[thr_id])
    cgpu = mining_thr[thr_id]->cgpu;
  rd_unlock(&mining_thr_lock);

  return cgpu;
}

struct cgpu_info *get_devices(int id)
{
  struct cgpu_info *cgpu = NULL;

  rd_lock(&devices_lock);
  if (id < total_devices)
    cgpu = devices[id];
  rd_unlock(&devices_lock);

  return cgpu;
}

void enable_device(int i);

static void sharelog(const char*disposition, const struct work*work)
{
  char *target, *hash, *data;
  struct cgpu_info *cgpu;
  unsigned long int t;
  struct pool *pool;
  int thr_id, rv;
  char s[1024];
  size_t ret;

  if (!sharelog_file)
    return;

  thr_id = work->thr_id;
  cgpu = get_thr_cgpu(thr_id);
  pool = work->pool;
  t = (unsigned long int)(work->tv_work_found.tv_sec);
  target = bin2hex(work->target, sizeof(work->target));
  hash = bin2hex(work->hash, sizeof(work->hash));
  data = bin2hex(work->data, sizeof(work->data));

  // timestamp,disposition,target,pool,dev,thr,sharehash,sharedata
  rv = snprintf(s, sizeof(s), "%lu,%s,%s,%s,%s%u,%u,%s,%s\n", t, disposition, target, pool->rpc_url, cgpu->drv->name, cgpu->device_id, thr_id, hash, data);
  free(target);
  free(hash);
  free(data);
  if (rv >= (int)(sizeof(s)))
    s[sizeof(s) - 1] = '\0';
  else if (rv < 0) {
    applog(LOG_ERR, "sharelog printf error");
    return;
  }

  mutex_lock(&sharelog_lock);
  ret = fwrite(s, rv, 1, sharelog_file);
  fflush(sharelog_file);
  mutex_unlock(&sharelog_lock);

  if (ret != 1)
    applog(LOG_ERR, "sharelog fwrite error");
}

static char *getwork_req = "{\"method\": \"getwork\", \"params\": [], \"id\":0}\n";

static char *gbt_req = "{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": [{\"capabilities\": [\"coinbasetxn\", \"workid\", \"coinbase/append\"]}]}\n";

/* Adjust all the pools' quota to the greatest common denominator after a pool
 * has been added or the quotas changed. */
void adjust_quota_gcd(void)
{
  unsigned long gcd, lowest_quota = ~0UL, quota;
  struct pool *pool;
  int i;

  for (i = 0; i < total_pools; i++) {
    pool = pools[i];
    quota = pool->quota;
    if (!quota)
      continue;
    if (quota < lowest_quota)
      lowest_quota = quota;
  }

  if (likely(lowest_quota < ~0UL)) {
    gcd = lowest_quota;
    for (i = 0; i < total_pools; i++) {
      pool = pools[i];
      quota = pool->quota;
      if (!quota)
        continue;
      while (quota % gcd)
        gcd--;
    }
  } else
    gcd = 1;

  for (i = 0; i < total_pools; i++) {
    pool = pools[i];
    pool->quota_used *= global_quota_gcd;
    pool->quota_used /= gcd;
    pool->quota_gcd = pool->quota / gcd;
  }

  global_quota_gcd = gcd;
  applog(LOG_DEBUG, "Global quota greatest common denominator set to %lu", gcd);
}

/* Return value is ignored if not called from add_pool_details */
struct pool *add_pool(void)
{
  struct pool *pool;

  pool = (struct pool *)calloc(sizeof(struct pool), 1);
  if (!pool)
    quit(1, "Failed to calloc pool in add_pool");
  pool->pool_no = pool->prio = total_pools;

  /* Default pool name is "" (empty string) */
  char buf[32];
  buf[0] = '\0';
  pool->name = strdup(buf);
  pool->profile = strdup(buf);  //profile blank by default
  pool->algorithm.name[0] = '\0'; //blank algorithm name

  /* intensities default to blank */
  pool->intensity = strdup(buf);
  pool->xintensity = strdup(buf);
  pool->rawintensity = strdup(buf);

  pools = (struct pool **)realloc(pools, sizeof(struct pool *) * (total_pools + 2));
  pools[total_pools++] = pool;
  mutex_init(&pool->pool_lock);
  if (unlikely(pthread_cond_init(&pool->cr_cond, NULL)))
    quit(1, "Failed to pthread_cond_init in add_pool");
  cglock_init(&pool->data_lock);
  mutex_init(&pool->stratum_lock);
  cglock_init(&pool->gbt_lock);
  INIT_LIST_HEAD(&pool->curlring);

  /* Make sure the pool doesn't think we've been idle since time 0 */
  pool->tv_idle.tv_sec = ~0UL;

  pool->rpc_req = getwork_req;
  pool->rpc_proxy = NULL;
  pool->quota = 1;
  adjust_quota_gcd();
  pool->extranonce_subscribe = true;

  pool->description = "";

  return pool;
}

/* Used in configuration parsing. */
static struct pool* get_current_pool()
{
  while ((json_array_index + 1) > total_pools)
    add_pool();

  if (json_array_index < 0) {
    if (!total_pools)
      add_pool();
    return pools[total_pools - 1];
  }

  return pools[json_array_index];
}

/* Used everywhere else (to get pool currently mined on). */
struct pool *current_pool(void)
{
  struct pool *pool;

  cg_rlock(&control_lock);
  pool = currentpool;
  cg_runlock(&control_lock);

  return pool;
}

/* Pool variant of test and set */
static bool pool_tset(struct pool *pool, bool *var)
{
  bool ret;

  mutex_lock(&pool->pool_lock);
  ret = *var;
  *var = true;
  mutex_unlock(&pool->pool_lock);

  return ret;
}

bool pool_tclear(struct pool *pool, bool *var)
{
  bool ret;

  mutex_lock(&pool->pool_lock);
  ret = *var;
  *var = false;
  mutex_unlock(&pool->pool_lock);

  return ret;
}

char *set_int_range(const char *arg, int *i, int min, int max)
{
  char *err = opt_set_intval(arg, i);

  if (err)
    return err;

  if (*i < min || *i > max)
    return "Value out of range";

  return NULL;
}

char *set_int_0_to_9999(const char *arg, int *i)
{
  return set_int_range(arg, i, 0, 9999);
}

char *set_int_1_to_65535(const char *arg, int *i)
{
  return set_int_range(arg, i, 1, 65535);
}

char *set_int_0_to_10(const char *arg, int *i)
{
  return set_int_range(arg, i, 0, 10);
}

char *set_int_1_to_10(const char *arg, int *i)
{
  return set_int_range(arg, i, 1, 10);
}

void get_intrange(char *arg, int *val1, int *val2)
{
  if (sscanf(arg, "%d-%d", val1, val2) == 1)
    *val2 = *val1;
}

char *set_devices(char *arg)
{
  int i, val1 = 0, val2 = 0;
  char *p, *nextptr;

  if(arg[0] != '\0')
  {
    if(!strcasecmp(arg, "?"))
    {
        opt_display_devs = true;
        return NULL;
    }
    //all devices enabled
    else if(!strcasecmp(arg, "*") || !strcasecmp(arg, "all"))
    {
        applog(LOG_DEBUG, "set_devices(all)");
        opt_devs_enabled = 0;
        return NULL;
    }
  }
  else
    return "Invalid device parameters";

  applog(LOG_DEBUG, "set_devices(%s)", arg);

  p = strdup(arg);
  nextptr = strtok(p, ",");

  do {
    if (nextptr == NULL)
    {
      free(p);
      return "Invalid parameters for set devices";
    }
    get_intrange(nextptr, &val1, &val2);

    if (val1 < 0 || val1 > MAX_DEVICES || val2 < 0 || val2 > MAX_DEVICES || val1 > val2)
    {
      free(p);
      return "Invalid value passed to set devices";
    }

    for (i = val1; i <= val2; i++)
    {
      devices_enabled[i] = true;
      opt_devs_enabled++;
    }
  } while ((nextptr = strtok(NULL, ",")) != NULL);

  applog(LOG_DEBUG, "set_devices(%s) done.", arg);

  free(p);
  return NULL;
}

static char *set_balance(enum pool_strategy *strategy)
{
  *strategy = POOL_BALANCE;
  return NULL;
}

static char *set_loadbalance(enum pool_strategy *strategy)
{
  *strategy = POOL_LOADBALANCE;
  return NULL;
}

static char *set_rotate(const char *arg, int *i)
{
  pool_strategy = POOL_ROTATE;
  return set_int_range(arg, i, 0, 9999);
}

static char *set_rr(enum pool_strategy *strategy)
{
  *strategy = POOL_ROUNDROBIN;
  return NULL;
}

/* Detect that url is for a stratum protocol either via the presence of
 * stratum+tcp or by detecting a stratum server response */
bool detect_stratum(struct pool *pool, char *url)
{
  if (!extract_sockaddr(url, &pool->sockaddr_url, &pool->stratum_port))
    return false;

  if (!strncasecmp(url, "stratum+tcp://", 14)) {
    pool->rpc_url = strdup(url);
    pool->has_stratum = true;
    pool->stratum_url = pool->sockaddr_url;
    return true;
  }

  return false;
}

static struct pool *add_url(void)
{
  total_urls++;
  if (total_urls > total_pools)
    add_pool();
  return pools[total_urls - 1];
}

static void setup_url(struct pool *pool, char *arg)
{
  arg = get_proxy(arg, pool);

  if (detect_stratum(pool, arg))
    return;

  opt_set_charp(arg, &pool->rpc_url);
  if (strncmp(arg, "http://", 7) && strncmp(arg, "https://", 8)) {
    char *httpinput;

    httpinput = (char *)malloc(255);
    if (!httpinput)
      quit(1, "Failed to malloc httpinput");
    strcpy(httpinput, "http://");
    strncat(httpinput, arg, 248);
    pool->rpc_url = httpinput;
  }
}

static char *set_url(char *arg)
{
  struct pool *pool = add_url();

  setup_url(pool, arg);
  return NULL;
}


static char *set_pool_algorithm(const char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Setting pool %i algorithm to %s", pool->pool_no, arg);
  set_algorithm(&pool->algorithm, arg);

  return NULL;
}

static char *set_pool_backup(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->backup = true;
  return NULL;
}

static char *set_pool_devices(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->devices = arg;
  return NULL;
}

static char *set_pool_kernelfile(const char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Setting pool %i algorithm kernel file to %s", pool->pool_no, arg);
  pool->algorithm.kernelfile = arg;

  return NULL;
}

static char *set_pool_lookup_gap(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->lookup_gap = arg;
  return NULL;
}

static char *set_pool_intensity(const char *arg)
{
  struct pool *pool = get_current_pool();
  opt_set_charp(arg, &pool->intensity);
  return NULL;
}

static char *set_pool_xintensity(const char *arg)
{
  struct pool *pool = get_current_pool();
  opt_set_charp(arg, &pool->xintensity);
  return NULL;
}

static char *set_pool_rawintensity(const char *arg)
{
  struct pool *pool = get_current_pool();
  opt_set_charp(arg, &pool->rawintensity);
  return NULL;
}

static char *set_pool_thread_concurrency(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->thread_concurrency = arg;
  return NULL;
}

#ifdef HAVE_ADL
static char *set_pool_gpu_engine(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->gpu_engine = arg;
  return NULL;
}

static char *set_pool_gpu_memclock(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->gpu_memclock = arg;
  return NULL;
}

static char *set_pool_gpu_threads(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->gpu_threads = arg;
  return NULL;
}

static char *set_pool_gpu_fan(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->gpu_fan = arg;
  return NULL;
}

static char *set_pool_gpu_powertune(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->gpu_powertune = arg;
  return NULL;
}

static char *set_pool_gpu_vddc(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->gpu_vddc = arg;
  return NULL;
}
#endif

static char *set_pool_nfactor(const char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Setting pool %i N-factor to %s", pool->pool_no, arg);
  set_algorithm_nfactor(&pool->algorithm, (const uint8_t) atoi(arg));

  return NULL;
}

static char *set_pool_name(char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Setting pool %i name to %s", pool->pool_no, arg);
  opt_set_charp(arg, &pool->name);

  return NULL;
}

static char *set_pool_profile(char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Setting pool %i profile to %s", pool->pool_no, arg);
  opt_set_charp(arg, &pool->profile);

  return NULL;
}

static char *set_poolname_deprecated(char *arg)
{
  applog(LOG_ERR, "Specifying pool name by --poolname is deprecated. Use --name instead.");
  set_pool_name(arg);

  return NULL;
}

static char *set_pool_shaders(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->shaders = arg;
  return NULL;
}

static char *set_pool_worksize(const char *arg)
{
  struct pool *pool = get_current_pool();
  pool->worksize = arg;
  return NULL;
}

static void enable_pool(struct pool *pool)
{
  if (pool->state != POOL_ENABLED)
    enabled_pools++;
  pool->state = POOL_ENABLED;
}

static void disable_pool(struct pool *pool)
{
  if (pool->state == POOL_ENABLED)
    enabled_pools--;
  pool->state = POOL_DISABLED;
}

static void reject_pool(struct pool *pool)
{
  if (pool->state == POOL_ENABLED)
    enabled_pools--;
  pool->state = POOL_REJECTING;
}

/* We can't remove the memory used for this struct pool because there may
 * still be work referencing it. We just remove it from the pools list */
void remove_pool(struct pool *pool)
{
  int i, last_pool = total_pools - 1;
  struct pool *other;

  /* Boost priority of any lower prio than this one */
  for (i = 0; i < total_pools; i++) {
    other = pools[i];
    if (other->prio > pool->prio)
      other->prio--;
  }

  if (pool->pool_no < last_pool) {
    /* Swap the last pool for this one */
    (pools[last_pool])->pool_no = pool->pool_no;
    pools[pool->pool_no] = pools[last_pool];
  }
  /* Give it an invalid number */
  pool->pool_no = total_pools;
  pool->removed = true;
  total_pools--;
}

static char *set_pool_state(char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_INFO, "Setting pool %s state to %s", get_pool_name(pool), arg);
  if (strcmp(arg, "disabled") == 0) {
    pool->state = POOL_DISABLED;
  } else if (strcmp(arg, "enabled") == 0) {
    pool->state = POOL_ENABLED;
  } else if (strcmp(arg, "hidden") == 0) {
    pool->state = POOL_HIDDEN;
  } else if (strcmp(arg, "rejecting") == 0) {
    pool->state = POOL_REJECTING;
  } else {
    pool->state = POOL_ENABLED;
  }

  return NULL;
}

static char *set_switcher_mode(char *arg)
{
  if(!strcasecmp(arg, "off"))
    opt_switchmode = SWITCH_OFF;
  else if(!strcasecmp(arg, "algorithm"))
    opt_switchmode = SWITCH_ALGO;
  else if(!strcasecmp(arg, "pool"))
    opt_switchmode = SWITCH_POOL;
  else
    return NULL;

  applog(LOG_INFO, "Setting switcher mode to %s", arg);
  return NULL;
}

static char *set_quota(char *arg)
{
  char *semicolon = strchr(arg, ';'), *url;
  size_t len, qlen;
  int quota;
  struct pool *pool;

  if (!semicolon)
    return "No semicolon separated quota;URL pair found";
  len = strlen(arg);
  *semicolon = '\0';
  qlen = strlen(arg);
  if (!qlen)
    return "No parameter for quota found";
  len -= qlen + 1;
  if (len < 1)
    return "No parameter for URL found";
  quota = atoi(arg);
  if (quota < 0)
    return "Invalid negative parameter for quota set";
  url = arg + qlen + 1;
  pool = add_url();
  setup_url(pool, url);
  pool->quota = quota;
  applog(LOG_INFO, "Setting %s to quota %d", get_pool_name(pool), pool->quota);
  adjust_quota_gcd();

  return NULL;
}

static char *set_user(const char *arg)
{
  struct pool *pool = get_current_pool();

  opt_set_charp(arg, &pool->rpc_user);

  return NULL;
}

static char *set_pass(const char *arg)
{
  struct pool *pool = get_current_pool();

  opt_set_charp(arg, &pool->rpc_pass);

  return NULL;
}

static char *set_userpass(const char *arg)
{
  struct pool *pool = get_current_pool();
  char *updup;

  updup = strdup(arg);
  opt_set_charp(arg, &pool->rpc_userpass);
  pool->rpc_user = strtok(updup, ":");
  if (!pool->rpc_user)
    return "Failed to find : delimited user info";
  pool->rpc_pass = strtok(NULL, ":");
  if (!pool->rpc_pass)
    pool->rpc_pass = "";

  return NULL;
}

static char *set_no_extranonce_subscribe(char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Disable extranonce subscribe on %d", pool->pool_no);
  opt_set_invbool(&pool->extranonce_subscribe);

  return NULL;
}

static char *set_pool_priority(char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Setting pool %i priority to %s", pool->pool_no, arg);
  opt_set_intval(arg, &pool->prio);

  return NULL;
}

static char *set_pool_description(char *arg)
{
  struct pool *pool = get_current_pool();

  applog(LOG_DEBUG, "Setting pool %i description to %s", pool->pool_no, arg);
  opt_set_charp(arg, &pool->description);

  return NULL;
}

static char *enable_debug(bool *flag)
{
  *flag = true;
  opt_debug_console = true;
  /* Turn on verbose output, too. */
  opt_verbose = true;
  return NULL;
}

static char *set_schedtime(const char *arg, struct schedtime *st)
{
  if (sscanf(arg, "%d:%d", &st->tm.tm_hour, &st->tm.tm_min) != 2)
    return "Invalid time set, should be HH:MM";
  if (st->tm.tm_hour > 23 || st->tm.tm_min > 59 || st->tm.tm_hour < 0 || st->tm.tm_min < 0)
    return "Invalid time set.";
  st->enable = true;
  return NULL;
}

static char *set_log_file(char *arg)
{
  char *r = "";
  long int i = strtol(arg, &r, 10);
  int fd, stderr_fd = fileno(stderr);

  if ((!*r) && i >= 0 && i <= INT_MAX)
    fd = i;
  else
  if (!strcmp(arg, "-"))
  {
    fd = fileno(stdout);
    if (unlikely(fd == -1))
      return "Standard output missing for log-file";
  }
  else
  {
    fd = open(arg, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
    if (unlikely(fd == -1))
      return "Failed to open %s for log-file";
  }

  close(stderr_fd);
  if (unlikely(-1 == dup2(fd, stderr_fd)))
    return "Failed to dup2 for log-file";
  close(fd);

  return NULL;
}

static char* set_sharelog(char *arg)
{
  char *r = "";
  long int i = strtol(arg, &r, 10);

  if ((!*r) && i >= 0 && i <= INT_MAX) {
    sharelog_file = fdopen((int)i, "a");
    if (!sharelog_file)
      applog(LOG_ERR, "Failed to open fd %u for share log", (unsigned int)i);
  } else if (!strcmp(arg, "-")) {
    sharelog_file = stdout;
    if (!sharelog_file)
      applog(LOG_ERR, "Standard output missing for share log");
  } else {
    sharelog_file = fopen(arg, "a");
    if (!sharelog_file)
      applog(LOG_ERR, "Failed to open %s for share log", arg);
  }

  return NULL;
}

static char *temp_cutoff_str = NULL;

char *set_temp_cutoff(char *arg)
{
  int val;

  if (!(arg && arg[0]))
    return "Invalid parameters for set temp cutoff";
  val = atoi(arg);
  if (val < 0 || val > 200)
    return "Invalid value passed to set temp cutoff";
  temp_cutoff_str = arg;

  return NULL;
}

static void load_temp_cutoffs()
{
  int i, val = 0, device = 0;
  char *nextptr;

  if (temp_cutoff_str) {
    for (device = 0, nextptr = strtok(temp_cutoff_str, ","); nextptr; ++device, nextptr = strtok(NULL, ",")) {
      if (device >= total_devices)
        quit(1, "Too many values passed to set temp cutoff");
      val = atoi(nextptr);
      if (val < 0 || val > 200)
        quit(1, "Invalid value passed to set temp cutoff");

      rd_lock(&devices_lock);
      devices[device]->cutofftemp = val;
      rd_unlock(&devices_lock);
    }
  } else {
    rd_lock(&devices_lock);
    for (i = device; i < total_devices; ++i) {
      if (!devices[i]->cutofftemp)
        devices[i]->cutofftemp = opt_cutofftemp;
    }
    rd_unlock(&devices_lock);

    return;
  }
  if (device <= 1) {
    rd_lock(&devices_lock);
    for (i = device; i < total_devices; ++i)
      devices[i]->cutofftemp = val;
    rd_unlock(&devices_lock);
  }
}

static char *set_api_allow(const char *arg)
{
  opt_set_charp(arg, &opt_api_allow);

  return NULL;
}

static char *set_api_groups(const char *arg)
{
  opt_set_charp(arg, &opt_api_groups);

  return NULL;
}

static char *set_api_description(const char *arg)
{
  opt_set_charp(arg, &opt_api_description);

  return NULL;
}

static char *set_api_mcast_addr(const char *arg)
{
  opt_set_charp(arg, &opt_api_mcast_addr);

  return NULL;
}

static char *set_api_mcast_code(const char *arg)
{
  opt_set_charp(arg, &opt_api_mcast_code);

  return NULL;
}

static char *set_api_mcast_des(const char *arg)
{
  opt_set_charp(arg, &opt_api_mcast_des);

  return NULL;
}

static char *set_null(const char __maybe_unused *arg)
{
  return NULL;
}

char *set_difficulty_multiplier(char *arg)
{
  applog(LOG_WARNING, "Option difficulty-multiplier is deprecated and will be removed in v5.1. Pools that need this option should fix their setup.");
  if (!(arg && arg[0]))
    return "Invalid parameters for set difficulty multiplier";
  opt_diff_mult = strtod(arg, NULL);
  if (opt_diff_mult == 0.0)
    return "Invalid value passed to set difficulty multiplier";

  return NULL;
}

/* These options are available from config file or commandline */
struct opt_table opt_config_table[] = {
  OPT_WITH_ARG("--algorithm|--kernel|-k",
         set_default_algorithm, NULL, NULL,
         "Set mining algorithm and most common defaults, default: scrypt"),
  OPT_WITH_ARG("--api-allow",
         set_api_allow, NULL, NULL,
         "Allow API access only to the given list of [G:]IP[/Prefix] addresses[/subnets]"),
  OPT_WITH_ARG("--api-description",
         set_api_description, NULL, NULL,
         "Description placed in the API status header, default: sgminer version"),
  OPT_WITH_ARG("--api-groups",
         set_api_groups, NULL, NULL,
         "API one letter groups G:cmd:cmd[,P:cmd:*...] defining the cmds a groups can use"),
  OPT_WITHOUT_ARG("--api-listen",
      opt_set_bool, &opt_api_listen,
      "Enable API, default: disabled"),
  OPT_WITHOUT_ARG("--api-mcast",
      opt_set_bool, &opt_api_mcast,
      "Enable API Multicast listener, default: disabled"),
  OPT_WITH_ARG("--api-mcast-addr",
     set_api_mcast_addr, NULL, NULL,
     "API Multicast listen address"),
  OPT_WITH_ARG("--api-mcast-code",
     set_api_mcast_code, NULL, NULL,
     "Code expected in the API Multicast message, don't use '-'"),
  OPT_WITH_ARG("--api-mcast-des",
     set_api_mcast_des, NULL, NULL,
     "Description appended to the API Multicast reply, default: ''"),
  OPT_WITH_ARG("--api-mcast-port",
     set_int_1_to_65535, opt_show_intval, &opt_api_mcast_port,
     "API Multicast listen port"),
  OPT_WITHOUT_ARG("--api-network",
      opt_set_bool, &opt_api_network,
      "Allow API (if enabled) to listen on/for any address, default: only 127.0.0.1"),
  OPT_WITH_ARG("--api-port",
      set_int_1_to_65535, opt_show_intval, &opt_api_port,
      "Port number of miner API"),
#ifdef HAVE_ADL
  OPT_WITHOUT_ARG("--auto-fan",
      opt_set_bool, &opt_autofan,
      "Automatically adjust all GPU fan speeds to maintain a target temperature"),
  OPT_WITHOUT_ARG("--auto-gpu",
      opt_set_bool, &opt_autoengine,
      "Automatically adjust all GPU engine clock speeds to maintain a target temperature"),
#endif
  OPT_WITHOUT_ARG("--balance",
      set_balance, &pool_strategy,
      "Change multipool strategy from failover to even share balance"),
  OPT_WITHOUT_ARG("--blake-compact",
      opt_set_bool, &opt_blake_compact,
      "Set SPH_COMPACT_BLAKE64 for Xn derived algorithms (Can give better hashrate for some GPUs)"),
#ifdef HAVE_CURSES
  OPT_WITHOUT_ARG("--compact",
      opt_set_bool, &opt_compact,
      "Use compact display without per device statistics"),
#endif
  OPT_WITHOUT_ARG("--debug|-D",
      enable_debug, &opt_debug,
      "Enable debug output"),
  OPT_WITHOUT_ARG("--debug-log",
      opt_set_bool, &opt_debug,
      "Enable debug logging when stderr is redirected to file"),
  OPT_WITH_ARG("--default-profile",
      set_default_profile, NULL, NULL,
      "Set Default Profile"),
  OPT_WITH_ARG("--description",
      set_pool_description, NULL, NULL,
      "Pool description"),
  OPT_WITH_ARG("--device|-d",
      set_default_devices, NULL, NULL,
      "Select device to use, one value, range and/or comma separated (e.g. 0-2,4) default: all"),
  OPT_WITHOUT_ARG("--disable-rejecting",
      opt_set_bool, &opt_disable_pool,
      "Automatically disable pools that continually reject shares"),
  OPT_WITH_ARG("--expiry|-E",
      set_int_0_to_9999, opt_show_intval, &opt_expiry,
      "Upper bound on how many seconds after getting work we consider a share from it stale"),

  // event options
  OPT_WITH_ARG("--event-on",
      set_event_type, NULL, NULL,
      "Select event type to perform task on"),
  OPT_WITH_ARG("--event-runcmd",
      set_event_runcmd, NULL, NULL,
      "Command to perform on event"),
  OPT_WITH_ARG("--event-reboot",
      set_event_reboot, NULL, NULL,
      "Reboot the system on event"),
  OPT_WITH_ARG("--event-reboot-delay",
      set_event_reboot_delay, NULL, NULL,
      "Delay in seconds to wait before rebooting"),
  OPT_WITH_ARG("--event-quit",
      set_event_quit, NULL, NULL,
      "Quit sgminer on event"),
  OPT_WITH_ARG("--event-quit-message",
      set_event_quit_message, NULL, NULL,
      "Quit message when quitting sgminer on event"),

  OPT_WITHOUT_ARG("--failover-only",
      opt_set_bool, &opt_fail_only,
      "Don't leak work to backup pools when primary pool is lagging"),
  OPT_WITH_ARG("--failover-switch-delay",
      set_int_1_to_65535, opt_show_intval, &opt_fail_switch_delay,
      "Delay in seconds before switching back to a failed pool"),
  OPT_WITHOUT_ARG("--fix-protocol",
      opt_set_bool, &opt_fix_protocol,
      "Do not redirect to a different getwork protocol (eg. stratum)"),
  OPT_WITH_ARG("--gpu-dyninterval",
      set_int_1_to_65535, opt_show_intval, &opt_dynamic_interval,
      "Set the refresh interval in ms for GPUs using dynamic intensity"),
  OPT_WITH_ARG("--gpu-platform",
      set_int_0_to_9999, opt_show_intval, &opt_platform_id,
      "Select OpenCL platform ID to use for GPU mining"),
#ifndef HAVE_ADL
  // gpu-threads can only be set per-card if ADL is available
  OPT_WITH_ARG("--gpu-threads|-g",
      set_int_1_to_10, opt_show_intval, &opt_g_threads,
      "Number of threads per GPU (1 - 10)"),
#else
  OPT_WITH_ARG("--gpu-threads|-g",
      set_default_gpu_threads, NULL, NULL,
      "Number of threads per GPU - one value or comma separated list (e.g. 1,2,1)"),
  OPT_WITH_ARG("--gpu-engine",
      set_default_gpu_engine, NULL, NULL,
      "GPU engine (over)clock range in Mhz - one value, range and/or comma separated list (e.g. 850-900,900,750-850)"),
  OPT_WITH_ARG("--gpu-fan",
      set_default_gpu_fan, NULL, NULL,
      "GPU fan percentage range - one value, range and/or comma separated list (e.g. 0-85,85,65)"),
  OPT_WITH_ARG("--gpu-map",
      set_gpu_map, NULL, NULL,
      "Map OpenCL to ADL device order manually, paired CSV (e.g. 1:0,2:1 maps OpenCL 1 to ADL 0, 2 to 1)"),
  OPT_WITH_ARG("--gpu-memclock",
      set_default_gpu_memclock, NULL, NULL,
      "Set the GPU memory (over)clock in Mhz - one value for all or separate by commas for per card"),
  OPT_WITH_ARG("--gpu-memdiff",
      set_gpu_memdiff, NULL, NULL,
      "Set a fixed difference in clock speed between the GPU and memory in auto-gpu mode"),
  OPT_WITH_ARG("--gpu-powertune",
      set_default_gpu_powertune, NULL, NULL,
      "Set the GPU powertune percentage - one value for all or separate by commas for per card"),
  OPT_WITHOUT_ARG("--gpu-reorder",
      opt_set_bool, &opt_reorder,
      "Attempt to reorder GPU devices according to PCI Bus ID"),
  OPT_WITH_ARG("--gpu-vddc",
      set_default_gpu_vddc, NULL, NULL,
      "Set the GPU voltage in Volts - one value for all or separate by commas for per card"),
#endif
  OPT_WITH_ARG("--hamsi-expand-big",
      set_int_1_to_10, opt_show_intval, &opt_hamsi_expand_big,
      "Set SPH_HAMSI_EXPAND_BIG for X13 derived algorithms (1 or 4 are common)"),
  OPT_WITHOUT_ARG("--hamsi-short",
      opt_set_bool, &opt_hamsi_short,
      "Set SPH_HAMSI_SHORT for X13 derived algorithms (Can give better hashrate for some GPUs)"),
  OPT_WITH_ARG("--keccak-unroll",
      set_int_0_to_9999, opt_show_intval, &opt_keccak_unroll,
      "Set SPH_KECCAK_UNROLL for Xn derived algorithms (Default: 0)"),
  OPT_WITH_ARG("--kernelfile",
         set_default_kernelfile, NULL, NULL,
         "Set the algorithm kernel source file (without file extension)."),
  OPT_WITH_ARG("--lookup-gap",
      set_default_lookup_gap, NULL, NULL,
      "Set GPU lookup gap for scrypt mining, comma separated"),
  OPT_WITHOUT_ARG("--luffa-parallel",
      opt_set_bool, &opt_luffa_parallel,
      "Set SPH_LUFFA_PARALLEL for Xn derived algorithms (Can give better hashrate for some GPUs)"),
#ifdef HAVE_CURSES
  OPT_WITHOUT_ARG("--incognito",
      opt_set_bool, &opt_incognito,
      "Do not display user name in status window"),
#endif
  OPT_WITHOUT_ARG("--more-notices",
      opt_set_bool, &opt_morenotices,
      "Shows work restart and new block notices, hidden by default"),
  OPT_WITH_ARG("--intensity|-I",
      set_default_intensity, NULL, NULL,
      "Intensity of GPU scanning (d or " MIN_INTENSITY_STR
      " -> " MAX_INTENSITY_STR
      ",default: d to maintain desktop interactivity), overridden by --xintensity or --rawintensity."),
  OPT_WITH_ARG("--xintensity|-X",
      set_default_xintensity, NULL, NULL,
      "Shader based intensity of GPU scanning (" MIN_XINTENSITY_STR " to "
        MAX_XINTENSITY_STR "), overridden --xintensity|-X and --rawintensity."),
  OPT_WITH_ARG("--rawintensity",
      set_default_rawintensity, NULL, NULL,
      "Raw intensity of GPU scanning (" MIN_RAWINTENSITY_STR " to "
        MAX_RAWINTENSITY_STR "), overrides --intensity|-I and --xintensity|-X."),
  OPT_WITH_ARG("--kernel-path|-K",
      opt_set_charp, opt_show_charp, &opt_kernel_path,
      "Specify a path to where kernel files are"),
  OPT_WITHOUT_ARG("--load-balance",
      set_loadbalance, &pool_strategy,
      "Change multipool strategy from failover to quota based balance"),
  OPT_WITH_ARG("--log|-l",
      set_int_0_to_9999, opt_show_intval, &opt_log_interval,
      "Interval in seconds between log output"),
 OPT_WITH_ARG("--log-file|-L",
      set_log_file, NULL, NULL,
      "Log stderr to file"),
  OPT_WITHOUT_ARG("--log-show-date|-L",
      opt_set_bool, &opt_log_show_date,
      "Show date on every log line"),
  OPT_WITHOUT_ARG("--lowmem",
      opt_set_bool, &opt_lowmem,
      "Minimise caching of shares for low memory applications"),
#if defined(unix) || defined(__APPLE__)
  OPT_WITH_ARG("--monitor|-m",
      opt_set_charp, NULL, &opt_stderr_cmd,
      "Use custom pipe cmd for output messages"),
#endif // defined(unix)
  OPT_WITH_ARG("--name|--pool-name",
      set_pool_name, NULL, NULL,
      "Name of pool"),
  OPT_WITHOUT_ARG("--net-delay",
      opt_set_bool, &opt_delaynet,
      "Impose small delays in networking to not overload slow routers"),
  OPT_WITH_ARG("--nfactor",
      set_default_nfactor, NULL, NULL,
      "Override default scrypt N-factor parameter."),
#ifdef HAVE_ADL
  OPT_WITHOUT_ARG("--no-adl",
      opt_set_bool, &opt_noadl,
      "Disable the ATI display library used for monitoring and setting GPU parameters"),
#else
  OPT_WITHOUT_ARG("--no-adl",
      opt_set_bool, &opt_noadl, opt_hidden),
#endif
  OPT_WITHOUT_ARG("--no-pool-disable",
      opt_set_invbool, &opt_disable_pool,
      opt_hidden),
  OPT_WITHOUT_ARG("--no-client-reconnect",
      opt_set_invbool, &opt_disable_client_reconnect,
      "Disable 'client.reconnect' stratum functionality"),
  OPT_WITHOUT_ARG("--no-restart",
      opt_set_invbool, &opt_restart,
      "Do not attempt to restart GPUs that hang"),
  OPT_WITHOUT_ARG("--no-submit-stale",
      opt_set_invbool, &opt_submit_stale,
      "Don't submit shares if they are detected as stale"),
  OPT_WITHOUT_ARG("--no-extranonce|--pool-no-extranonce",
      set_no_extranonce_subscribe, NULL,
      "Disable 'extranonce' stratum subscribe for pool"),
  OPT_WITH_ARG("--pass|--pool-pass|-p",
      set_pass, NULL, NULL,
      "Password for bitcoin JSON-RPC server"),
  OPT_WITHOUT_ARG("--per-device-stats",
      opt_set_bool, &want_per_device_stats,
      "Force verbose mode and output per-device statistics"),

  OPT_WITH_ARG("--poolname", /* TODO: Backward compatibility, to be removed. */
      set_poolname_deprecated, NULL, NULL,
      opt_hidden),
  OPT_WITH_ARG("--pool-algorithm|--pool-kernel",
      set_pool_algorithm, NULL, NULL,
      "Set algorithm for pool"),
  OPT_WITHOUT_ARG("--pool-backup",
      set_pool_backup, NULL,
      "Mark this pool as a backup for simple connect strategy"),
  OPT_WITH_ARG("--pool-device",
      set_pool_devices, NULL, NULL,
      "Select devices to use with pool, one value, range and/or comma separated (e.g. 0-2,4) default: all"),
  OPT_WITH_ARG("--pool-kernelfile",
      set_pool_kernelfile, NULL, NULL,
      "Set the pool's algorithm kernel source file (without file extension)."),
  OPT_WITH_ARG("--pool-lookup-gap",
      set_pool_lookup_gap, NULL, NULL,
      "Set Pool GPU lookup gap for scrypt mining, comma separated"),
#ifdef HAVE_ADL
  OPT_WITH_ARG("--pool-gpu-engine",
      set_pool_gpu_engine, NULL, NULL,
      "Pool GPU engine (over)clock range in Mhz - one value, range and/or comma separated list (e.g. 850-900,900,750-850)"),
  OPT_WITH_ARG("--pool-gpu-fan",
      set_pool_gpu_fan, NULL, NULL,
      "GPU fan for pool"),
  OPT_WITH_ARG("--pool-gpu-memclock",
      set_pool_gpu_memclock, NULL, NULL,
      "Set the Pool GPU memory (over)clock in Mhz - one value for all or separate by commas for per card"),
  OPT_WITH_ARG("--pool-gpu-powertune",
      set_pool_gpu_powertune, NULL, NULL,
      "Set the Pool GPU powertune percentage - one value for all or separate by commas for per card"),
  OPT_WITH_ARG("--pool-gpu-threads",
      set_pool_gpu_threads, NULL, NULL,
      "Number of threads per GPU for pool"),
  OPT_WITH_ARG("--pool-gpu-vddc",
      set_pool_gpu_vddc, NULL, NULL,
      "Set the Pool GPU voltage in Volts - one value for all or separate by commas for per card"),
#endif
  OPT_WITH_ARG("--pool-intensity",
      set_pool_intensity, NULL, NULL,
      "Intensity of GPU scanning (pool-specific)"),
  OPT_WITH_ARG("--pool-nfactor",
      set_pool_nfactor, NULL, NULL,
      "Set N-factor for pool"),
  OPT_WITH_ARG("--pool-profile",
      set_pool_profile, NULL, NULL,
      "Profile to use with the pool"),
  OPT_WITH_ARG("--pool-rawintensity",
      set_pool_rawintensity, NULL, NULL,
      "Raw intensity of GPU scanning (pool-specific)"),
  OPT_WITH_ARG("--pool-shaders",
      set_pool_shaders, NULL, NULL,
      "Pool GPU shaders per card for tuning scrypt, comma separated"),
  OPT_WITH_ARG("--pool-thread-concurrency",
      set_pool_thread_concurrency, NULL, NULL,
      "Set thread concurrency for pool"),
  OPT_WITH_ARG("--pool-worksize",
      set_pool_worksize, NULL, NULL,
      "Override detected optimal worksize for pool - one value or comma separated list"),
  OPT_WITH_ARG("--pool-xintensity",
      set_pool_xintensity, NULL, NULL,
      "Shader based intensity of GPU scanning (pool-specific)"),

  OPT_WITH_ARG("--priority|--pool-priority",
       set_pool_priority, NULL, NULL,
       "Pool priority"),

  OPT_WITH_ARG("--profile-algorithm|--profile-kernel",
      set_profile_algorithm, NULL, NULL,
      "Set algorithm for profile"),
  OPT_WITH_ARG("--profile-device",
      set_profile_devices, NULL, NULL,
      "Select devices to use with profile, one value, range and/or comma separated (e.g. 0-2,4) default: all"),
  OPT_WITH_ARG("--profile-kernelfile",
      set_profile_kernelfile, NULL, NULL,
      "Set the profile's algorithm kernel source file (without file extension)."),
  OPT_WITH_ARG("--profile-lookup-gap",
      set_profile_lookup_gap, NULL, NULL,
      "Set Profile GPU lookup gap for scrypt mining, comma separated"),
#ifdef HAVE_ADL
  OPT_WITH_ARG("--profile-gpu-engine",
      set_profile_gpu_engine, NULL, NULL,
      "Profile GPU engine (over)clock range in Mhz - one value, range and/or comma separated list (e.g. 850-900,900,750-850)"),
  OPT_WITH_ARG("--profile-gpu-fan",
      set_profile_gpu_fan, NULL, NULL,
      "GPU fan for profile"),
  OPT_WITH_ARG("--profile-gpu-memclock",
      set_profile_gpu_memclock, NULL, NULL,
      "Set the Profile GPU memory (over)clock in Mhz - one value for all or separate by commas for per card"),
  OPT_WITH_ARG("--profile-gpu-powertune",
      set_profile_gpu_powertune, NULL, NULL,
      "Set the Profile GPU powertune percentage - one value for all or separate by commas for per card"),
  OPT_WITH_ARG("--profile-gpu-threads",
      set_profile_gpu_threads, NULL, NULL,
      "Number of threads per GPU for profile"),
  OPT_WITH_ARG("--profile-gpu-vddc",
      set_profile_gpu_vddc, NULL, NULL,
      "Set the Profile GPU voltage in Volts - one value for all or separate by commas for per card"),
#endif
  OPT_WITH_ARG("--profile-intensity",
      set_profile_intensity, NULL, NULL,
      "Intensity of GPU scanning (profile-specific)"),
  OPT_WITH_ARG("--profile-name",
      set_profile_name, NULL, NULL,
      "Profile Name"),
  OPT_WITH_ARG("--profile-nfactor",
      set_profile_nfactor, NULL, NULL,
      "Set N-factor for profile"),
  OPT_WITH_ARG("--profile-rawintensity",
      set_profile_rawintensity, NULL, NULL,
      "Raw intensity of GPU scanning (profile-specific)"),
  OPT_WITH_ARG("--profile-shaders",
      set_profile_shaders, NULL, NULL,
      "Profile GPU shaders per card for tuning scrypt, comma separated"),
  OPT_WITH_ARG("--profile-thread-concurrency",
      set_profile_thread_concurrency, NULL, NULL,
      "Set thread concurrency for profile"),
  OPT_WITH_ARG("--profile-worksize",
      set_profile_worksize, NULL, NULL,
      "Override detected optimal worksize for profile - one value or comma separated list"),
  OPT_WITH_ARG("--profile-xintensity",
      set_profile_xintensity, NULL, NULL,
      "Shader based intensity of GPU scanning (profile-specific)"),

  OPT_WITHOUT_ARG("--protocol-dump|-P",
      opt_set_bool, &opt_protocol,
      "Verbose dump of protocol-level activities"),
  OPT_WITH_ARG("--queue|-Q",
      set_int_0_to_9999, opt_show_intval, &opt_queue,
      "Minimum number of work items to have queued (0+)"),
  OPT_WITHOUT_ARG("--quiet|-q",
      opt_set_bool, &opt_quiet,
      "Disable logging output, display status and errors"),
  OPT_WITH_ARG("--quota|--pool-quota|-U",
      set_quota, NULL, NULL,
      "quota;URL combination for server with load-balance strategy quotas"),
  OPT_WITHOUT_ARG("--real-quiet",
      opt_set_bool, &opt_realquiet,
      "Disable all output"),
  OPT_WITHOUT_ARG("--remove-disabled",
      opt_set_bool, &opt_removedisabled,
      "Remove disabled devices entirely, as if they didn't exist"),
  OPT_WITH_ARG("--retries",
      set_null, NULL, NULL,
      opt_hidden),
  OPT_WITH_ARG("--retry-pause",
      set_null, NULL, NULL,
      opt_hidden),
  OPT_WITH_ARG("--rotate",
      set_rotate, opt_show_intval, &opt_rotate_period,
      "Change multipool strategy from failover to regularly rotate at N minutes"),
  OPT_WITHOUT_ARG("--round-robin",
      set_rr, &pool_strategy,
      "Change multipool strategy from failover to round robin on failure"),
  OPT_WITH_ARG("--scan-time|-s",
      set_int_0_to_9999, opt_show_intval, &opt_scantime,
      "Upper bound on time spent scanning current work, in seconds"),
  OPT_WITH_ARG("--sched-start",
      set_schedtime, NULL, &schedstart,
      "Set a time of day in HH:MM to start mining (a once off without a stop time)"),
  OPT_WITH_ARG("--sched-stop",
      set_schedtime, NULL, &schedstop,
      "Set a time of day in HH:MM to stop mining (will quit without a start time)"),
  OPT_WITH_ARG("--shaders",
      set_default_shaders, NULL, NULL,
      "GPU shaders per card for tuning scrypt, comma separated"),
  OPT_WITH_ARG("--sharelog",
      set_sharelog, NULL, NULL,
      "Append share log to file"),
  OPT_WITH_ARG("--shares",
      opt_set_intval, NULL, &opt_shares,
      "Quit after mining N shares (default: unlimited)"),
  OPT_WITH_ARG("--socks-proxy",
      opt_set_charp, NULL, &opt_socks_proxy,
      "Set socks4 proxy (host:port)"),
  OPT_WITHOUT_ARG("--show-coindiff",
      opt_set_bool, &opt_show_coindiff,
      "Show coin difficulty rather than hash value of a share"),
  OPT_WITH_ARG("--state|--pool-state",
      set_pool_state, NULL, NULL,
      "Specify pool state at startup (default: enabled)"),
  OPT_WITH_ARG("--switcher-mode",
      set_switcher_mode, NULL, NULL,
      "Algorithm/gpu settings switcher mode."),
#ifdef HAVE_SYSLOG_H
  OPT_WITHOUT_ARG("--syslog",
      opt_set_bool, &use_syslog,
      "Use system log for output messages (default: standard error)"),
#endif
#if defined(HAVE_LIBCURL) && defined(CURL_HAS_KEEPALIVE)
  OPT_WITH_ARG("--tcp-keepalive",
      set_int_0_to_9999, opt_show_intval, &opt_tcp_keepalive,
      "TCP keepalive packet idle time"),
#else
  OPT_WITH_ARG("--tcp-keepalive",
      set_int_0_to_9999, opt_show_intval, &opt_tcp_keepalive,
      opt_hidden),
#endif
#ifdef HAVE_ADL
  OPT_WITH_ARG("--temp-cutoff",
      set_temp_cutoff, opt_show_intval, &opt_cutofftemp,
      "Temperature which a device will be automatically disabled at, one value or comma separated list"),
  OPT_WITH_ARG("--temp-hysteresis",
      set_int_1_to_10, opt_show_intval, &opt_hysteresis,
      "Set how much the temperature can fluctuate outside limits when automanaging speeds"),
  OPT_WITH_ARG("--temp-overheat",
      set_temp_overheat, opt_show_intval, &opt_overheattemp,
      "Temperature which a device will be throttled at while automanaging fan and/or GPU, one value or comma separated list"),
  OPT_WITH_ARG("--temp-target",
      set_temp_target, opt_show_intval, &opt_targettemp,
      "Temperature which a device should stay at while automanaging fan and/or GPU, one value or comma separated list"),
#endif
#ifdef HAVE_CURSES
  OPT_WITHOUT_ARG("--text-only|-T",
      opt_set_invbool, &use_curses,
      "Disable ncurses formatted screen output"),
#else
  OPT_WITHOUT_ARG("--text-only|-T",
      opt_set_invbool, &use_curses,
      opt_hidden),
#endif
  OPT_WITH_ARG("--thread-concurrency",
      set_default_thread_concurrency, NULL, NULL,
      "Set GPU thread concurrency for scrypt mining, comma separated"),
  OPT_WITH_ARG("--url|--pool-url|-o",
      set_url, NULL, NULL,
      "URL for bitcoin JSON-RPC server"),
  OPT_WITH_ARG("--user|--pool-user|-u",
      set_user, NULL, NULL,
      "Username for bitcoin JSON-RPC server"),
  OPT_WITH_ARG("--vectors",
      set_vector, NULL, NULL,
      opt_hidden),
      /* All current kernels only support vectors=1 */
      /* "Override detected optimal vector (1, 2 or 4) - one value or comma separated list"), */
  OPT_WITHOUT_ARG("--verbose|-v",
      opt_set_bool, &opt_verbose,
      "Log verbose output to stderr as well as status output"),
  OPT_WITH_ARG("--vote",
      set_int_1_to_65535, opt_show_intval, &opt_vote,
      "Optional vote value for decred blocks"),
  OPT_WITH_ARG("--watchpool-refresh",
      set_int_1_to_65535, opt_show_intval, &opt_watchpool_refresh,
      "Interval in seconds to refresh pool status"),
  OPT_WITH_ARG("--worksize|-w",
      set_default_worksize, NULL, NULL,
      "Override detected optimal worksize - one value or comma separated list"),
  OPT_WITH_ARG("--userpass|--pool-userpass|-O",
      set_userpass, NULL, NULL,
      "Username:Password pair for bitcoin JSON-RPC server"),
  OPT_WITHOUT_ARG("--worktime",
      opt_set_bool, &opt_worktime,
      "Display extra work time debug information"),
  OPT_WITH_ARG("--pools",
      opt_set_bool, NULL, NULL, opt_hidden),
  OPT_WITH_ARG("--profiles",
      opt_set_bool, NULL, NULL, opt_hidden),
  OPT_WITH_ARG("--includes",
      opt_set_bool, NULL, NULL, opt_hidden),
  OPT_WITH_ARG("--events",
      opt_set_bool, NULL, NULL, opt_hidden),
  OPT_WITH_ARG("--difficulty-multiplier",
      set_difficulty_multiplier, NULL, NULL,
      "(deprecated) Difficulty multiplier for jobs received from stratum pools"),
  OPT_ENDTABLE
};

void default_save_file(char *filename);

extern const char *opt_argv0;

static char *opt_verusage_and_exit(const char *extra)
{
  printf("%s\n", packagename);
  printf("%s", opt_usage(opt_argv0, extra));
  fflush(stdout);
  exit(0);
}

char *display_devs(int *ndevs)
{
  *ndevs = 0;
  print_ndevs(ndevs);
  exit(*ndevs);
}

/* These options are available from commandline only */
static struct opt_table opt_cmdline_table[] = {
  OPT_WITH_ARG("--config|-c",
      load_config, NULL, NULL,
      "Load a JSON-format configuration file\n"
      "See example.conf for an example configuration."),
  OPT_WITH_ARG("--default-config",
      set_default_config, NULL, NULL,
      "Specify the filename of the default config file\n"
      "Loaded at start and used when saving without a name."),
  OPT_WITH_ARG("--remote-config-retry",
      set_int_0_to_9999, opt_show_intval, &opt_remoteconf_retry,
      "Number of times to retry downloading remote config file. Default: 3"),
  OPT_WITH_ARG("--remote-config-wait",
      set_int_0_to_9999, opt_show_intval, &opt_remoteconf_wait,
      "Time in seconds to wait between download retries of remote config files. Default: 10secs"),
  OPT_WITHOUT_ARG("--remote-config-usecache",
      opt_set_bool, &opt_remoteconf_usecache,
      "Use cached copy of the remote config file when download fails. Default: No"),
  OPT_WITHOUT_ARG("--help|-h",
      opt_verusage_and_exit, NULL,
      "Print this message"),
  OPT_WITHOUT_ARG("--ndevs|-n",
      display_devs, &nDevs,
      "Display number of detected GPUs, OpenCL platform "
      "information, and exit"),
  OPT_WITHOUT_ARG("--version|-V",
      opt_version_and_exit, packagename,
      "Display version and exit"),
  OPT_ENDTABLE
};

#ifdef HAVE_LIBCURL
static bool jobj_binary(const json_t *obj, const char *key,
      void *buf, size_t buflen, bool required)
{
  const char *hexstr;
  json_t *tmp;

  tmp = json_object_get(obj, key);
  if (unlikely(!tmp)) {
    if (unlikely(required))
      if (opt_morenotices)
        applog(LOG_ERR, "JSON key '%s' not found", key);
    return false;
  }
  hexstr = json_string_value(tmp);
  if (unlikely(!hexstr)) {
    applog(LOG_ERR, "JSON key '%s' is not a string", key);
    return false;
  }
  if (!hex2bin((unsigned char *)buf, hexstr, buflen))
    return false;

  return true;
}
#endif

static struct work *make_work(void)
{
  struct work *w = (struct work *)calloc(sizeof(struct work), 1);

  if (unlikely(!w))
    quit(1, "Failed to calloc work in make_work");

  cg_wlock(&control_lock);
  w->id = total_work++;
  cg_wunlock(&control_lock);

  return w;
}

/* This is the central place all work that is about to be retired should be
 * cleaned to remove any dynamically allocated arrays within the struct */
void clean_work(struct work *w)
{
  free(w->job_id);
  free(w->ntime);
  free(w->coinbase);
  free(w->nonce1);
  memset(w, 0, sizeof(struct work));
}

/* All dynamically allocated work structs should be freed here to not leak any
 * ram from arrays allocated within the work struct */
void free_work(struct work *w)
{
  clean_work(w);
  free(w);
}

static void calc_diff(struct work *work, double known);

#ifdef HAVE_LIBCURL
/* Process transactions with GBT by storing the binary value of the first
 * transaction, and the hashes of the remaining transactions since these
 * remain constant with an altered coinbase when generating work. Must be
 * entered under gbt_lock */
static bool __build_gbt_txns(struct pool *pool, json_t *res_val)
{
  json_t *txn_array;
  bool ret = false;
  size_t cal_len;
  int i;

  free(pool->txn_hashes);
  pool->txn_hashes = NULL;
  pool->gbt_txns = 0;

  txn_array = json_object_get(res_val, "transactions");
  if (!json_is_array(txn_array))
    goto out;

  ret = true;
  pool->gbt_txns = json_array_size(txn_array);
  if (!pool->gbt_txns)
    goto out;

  pool->txn_hashes = (unsigned char *)calloc(32 * (pool->gbt_txns + 1), 1);
  if (unlikely(!pool->txn_hashes))
    quit(1, "Failed to calloc txn_hashes in __build_gbt_txns");

  for (i = 0; i < pool->gbt_txns; i++) {
    json_t *txn_val = json_object_get(json_array_get(txn_array, i), "data");
    const char *txn = json_string_value(txn_val);
    size_t txn_len = strlen(txn);
    unsigned char *txn_bin;

    cal_len = txn_len;
    align_len(&cal_len);
    txn_bin = (unsigned char *)calloc(cal_len, 1);
    if (unlikely(!txn_bin))
      quit(1, "Failed to calloc txn_bin in __build_gbt_txns");
    if (unlikely(!hex2bin(txn_bin, txn, txn_len / 2)))
      quit(1, "Failed to hex2bin txn_bin");

    gen_hash(txn_bin, txn_len / 2, pool->txn_hashes + (32 * i));
    free(txn_bin);
  }
out:
  return ret;
}

static unsigned char *__gbt_merkleroot(struct pool *pool)
{
  unsigned char *merkle_hash;
  int i, txns;

  merkle_hash = (unsigned char *)calloc(32 * (pool->gbt_txns + 2), 1);
  if (unlikely(!merkle_hash))
    quit(1, "Failed to calloc merkle_hash in __gbt_merkleroot");

  gen_hash(pool->coinbase, pool->coinbase_len, merkle_hash);

  if (pool->gbt_txns)
    memcpy(merkle_hash + 32, pool->txn_hashes, pool->gbt_txns * 32);

  txns = pool->gbt_txns + 1;
  while (txns > 1) {
    if (txns % 2) {
      memcpy(&merkle_hash[txns * 32], &merkle_hash[(txns - 1) * 32], 32);
      txns++;
    }
    for (i = 0; i < txns; i += 2){
      unsigned char hashout[32];

      gen_hash(merkle_hash + (i * 32), 64, hashout);
      memcpy(merkle_hash + (i / 2 * 32), hashout, 32);
    }
    txns /= 2;
  }
  return merkle_hash;
}

static bool work_decode(struct pool *pool, struct work *work, json_t *val);

static void update_gbt(struct pool *pool)
{
  int rolltime;
  json_t *val;
  CURL *curl;
  char curl_err_str[CURL_ERROR_SIZE];

  curl = curl_easy_init();
  if (unlikely(!curl))
    quit (1, "CURL initialisation failed in update_gbt");

  val = json_rpc_call(curl, curl_err_str, pool->rpc_url, pool->rpc_userpass,
          pool->rpc_req, true, false, &rolltime, pool, false);

  if (val) {
    struct work *work = make_work();
    bool rc = work_decode(pool, work, val);

    total_getworks++;
    pool->getwork_requested++;
    if (rc) {
      applog(LOG_DEBUG, "Successfully retrieved and updated GBT from %s", get_pool_name(pool));
      cgtime(&pool->tv_idle);
      if (pool == current_pool())
        opt_work_update = true;
    } else {
      applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher GBT from %s", get_pool_name(pool));
    }
    json_decref(val);
    free_work(work);
  } else {
    applog(LOG_DEBUG, "FAILED to update GBT from %s", get_pool_name(pool));
  }
  curl_easy_cleanup(curl);
}

/* Return the work coin/network difficulty */
static double get_work_blockdiff(const struct work *work)
{
  uint32_t* data = (uint32_t*) work->data;
  uint64_t diff64;
  double numerator;
  int powdiff;
  uint8_t shift;

  if (work->pool->algorithm.type == ALGO_ETHASH) {
    return 0;//work->network_diff;
  }
  // Neoscrypt has the data reversed
  if (work->pool->algorithm.type == ALGO_NEOSCRYPT || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
      work->pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI) {
    diff64 = bswap_64(((uint64_t)(be32toh(*((uint32_t *)(work->data + 72))) & 0xFFFFFF00)) << 8);
    numerator = (double)work->pool->algorithm.diff_numerator;
  }
  else if (work->pool->algorithm.type == ALGO_DECRED) {
    shift = work->data[116+3];
    powdiff = (8 * (0x1d - 3)) - (8 * (shift - 3));
    diff64 = data[29] & 0xFFFFFF;
    if (!diff64) diff64 = 1;
    double d = (double)work->pool->algorithm.diff_numerator / (double)diff64;
    for (int m = shift; m < 29; m++) d *= 256.0;
    for (int m = 29; m < shift; m++) d /= 256.0;
    if (shift == 28) d *= 256.0; // testnet
    return d;
  }
  else {
    shift = work->data[72];
    powdiff = (8 * (0x1d - 3)) - (8 * (shift - 3));;
    diff64 = be32toh(*((uint32_t *)(work->data + 72))) & 0x0000000000FFFFFF;
    numerator = work->pool->algorithm.diff_numerator << powdiff;
  }

  if (unlikely(!diff64)) {
    diff64 = 1;
  }

  return numerator / (double)diff64;
}

static void gen_gbt_work(struct pool *pool, struct work *work)
{
  unsigned char *merkleroot;
  struct timeval now;
  uint64_t nonce2le;

  cgtime(&now);
  if (now.tv_sec - pool->tv_lastwork.tv_sec > 60)
    update_gbt(pool);

  cg_wlock(&pool->gbt_lock);
  nonce2le = htole64(pool->nonce2);
  memcpy(pool->coinbase + pool->nonce2_offset, &nonce2le, pool->n2size);
  pool->nonce2++;
  cg_dwlock(&pool->gbt_lock);
  merkleroot = __gbt_merkleroot(pool);

  memcpy(work->data, &pool->gbt_version, 4);
  memcpy(work->data + 4, pool->previousblockhash, 32);
  memcpy(work->data + 4 + 32 + 32, &pool->curtime, 4);
  memcpy(work->data + 4 + 32 + 32 + 4, &pool->gbt_bits, 4);

  memcpy(work->target, pool->gbt_target, 32);

  work->coinbase = bin2hex(pool->coinbase, pool->coinbase_len);

  /* For encoding the block data on submission */
  work->gbt_txns = pool->gbt_txns + 1;

  if (pool->gbt_workid)
    work->job_id = strdup(pool->gbt_workid);
  cg_runlock(&pool->gbt_lock);

  flip32(work->data + 4 + 32, merkleroot);
  free(merkleroot);
  memset(work->data + 4 + 32 + 32 + 4 + 4, 0, 4 + 48); /* nonce + padding */

  if (opt_debug) {
    char *header = bin2hex(work->data, 128);

    applog(LOG_DEBUG, "Generated GBT header %s", header);
    applog(LOG_DEBUG, "Work coinbase %s", work->coinbase);
    free(header);
  }

  if (pool->algorithm.calc_midstate) pool->algorithm.calc_midstate(work);
  local_work++;
  work->pool = pool;
  work->gbt = true;
  work->id = total_work++;
  work->longpoll = false;
  work->getwork_mode = GETWORK_MODE_GBT;
  work->work_block = work_block;
  /* Nominally allow a driver to ntime roll 60 seconds */
  work->drv_rolllimit = 60;
  calc_diff(work, 0);
  cgtime(&work->tv_staged);
}

static bool gbt_decode(struct pool *pool, json_t *res_val)
{
  const char *previousblockhash;
  const char *target;
  const char *coinbasetxn;
  const char *longpollid;
  unsigned char hash_swap[32];
  int expires;
  int version;
  int curtime;
  bool submitold;
  const char *bits;
  const char *workid;
  size_t cbt_len, orig_len;
  uint8_t *extra_len;
  size_t cal_len;

  previousblockhash = json_string_value(json_object_get(res_val, "previousblockhash"));
  target = json_string_value(json_object_get(res_val, "target"));
  coinbasetxn = json_string_value(json_object_get(json_object_get(res_val, "coinbasetxn"), "data"));
  longpollid = json_string_value(json_object_get(res_val, "longpollid"));
  expires = json_integer_value(json_object_get(res_val, "expires"));
  version = json_integer_value(json_object_get(res_val, "version"));
  curtime = json_integer_value(json_object_get(res_val, "curtime"));
  submitold = json_is_true(json_object_get(res_val, "submitold"));
  bits = json_string_value(json_object_get(res_val, "bits"));
  workid = json_string_value(json_object_get(res_val, "workid"));

  if (!previousblockhash || !target || !coinbasetxn || !longpollid ||
      !expires || !version || !curtime || !bits) {
    applog(LOG_ERR, "JSON failed to decode GBT");
    return false;
  }

  applog(LOG_DEBUG, "previousblockhash: %s", previousblockhash);
  applog(LOG_DEBUG, "target: %s", target);
  applog(LOG_DEBUG, "coinbasetxn: %s", coinbasetxn);
  applog(LOG_DEBUG, "longpollid: %s", longpollid);
  applog(LOG_DEBUG, "expires: %d", expires);
  applog(LOG_DEBUG, "version: %d", version);
  applog(LOG_DEBUG, "curtime: %d", curtime);
  applog(LOG_DEBUG, "submitold: %s", submitold ? "true" : "false");
  applog(LOG_DEBUG, "bits: %s", bits);
  if (workid)
    applog(LOG_DEBUG, "workid: %s", workid);

  cg_wlock(&pool->gbt_lock);
  free(pool->coinbasetxn);
  pool->coinbasetxn = strdup(coinbasetxn);
  cbt_len = strlen(pool->coinbasetxn) / 2;
  /* We add 8 bytes of extra data corresponding to nonce2 */
  pool->n2size = 8;
  pool->coinbase_len = cbt_len + pool->n2size;
  cal_len = pool->coinbase_len + 1;
  align_len(&cal_len);
  free(pool->coinbase);
  pool->coinbase = (unsigned char *)calloc(cal_len, 1);
  if (unlikely(!pool->coinbase))
    quit(1, "Failed to calloc pool coinbase in gbt_decode");
  hex2bin(pool->coinbase, pool->coinbasetxn, 42);
  extra_len = (uint8_t *)(pool->coinbase + 41);
  orig_len = *extra_len;
  hex2bin(pool->coinbase + 42, pool->coinbasetxn + 84, orig_len);
  *extra_len += pool->n2size;
  hex2bin(pool->coinbase + 42 + *extra_len, pool->coinbasetxn + 84 + (orig_len * 2),
    cbt_len - orig_len - 42);
  pool->nonce2_offset = orig_len + 42;

  free(pool->longpollid);
  pool->longpollid = strdup(longpollid);
  free(pool->gbt_workid);
  if (workid)
    pool->gbt_workid = strdup(workid);
  else
    pool->gbt_workid = NULL;

  hex2bin(hash_swap, previousblockhash, 32);
  swap256(pool->previousblockhash, hash_swap);

  hex2bin(hash_swap, target, 32);
  swab256(pool->gbt_target, hash_swap);

  pool->gbt_expires = expires;
  pool->gbt_version = htobe32(version);
  pool->curtime = htobe32(curtime);
  pool->submit_old = submitold;

  hex2bin((unsigned char *)&pool->gbt_bits, bits, 4);

  __build_gbt_txns(pool, res_val);
  cg_wunlock(&pool->gbt_lock);

  return true;
}

/* truediffone == 0x00000000FFFF0000000000000000000000000000000000000000000000000000
 * Generate a 256 bit binary LE target by cutting up diff into 64 bit sized
 * portions or vice versa. */
const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
const double bits128 = 340282366920938463463374607431768211456.0;
const double bits64 = 18446744073709551616.0;

/* Converts a little endian 256 bit value to a double */
static double le256todouble(const void *target)
{
  uint64_t *data64;
  double dcut64;

  data64 = (uint64_t *)((unsigned char *)target + 24);
  dcut64 = le64toh(*data64) * bits192;

  data64 = (uint64_t *)((unsigned char *)target + 16);
  dcut64 += le64toh(*data64) * bits128;

  data64 = (uint64_t *)((unsigned char *)target + 8);
  dcut64 += le64toh(*data64) * bits64;

  data64 = (uint64_t *)target;
  dcut64 += le64toh(*data64);

  return dcut64;
}

double le256todiff(const void *le256, double diff_multiplier) {
  double d64 = diff_multiplier * truediffone;
  double s64 = le256todouble(le256);
  return d64 / (s64 + 1.);
}

static bool getwork_decode(json_t *res_val, struct work *work)
{
  size_t worklen = 128;
  if (work->pool->algorithm.type == ALGO_CRE) worklen = 168;
  else if (work->pool->algorithm.type == ALGO_DECRED) worklen = 192;
  else if (work->pool->algorithm.type == ALGO_PHI2 || work->pool->algorithm.type == ALGO_PHI2_NAVI) worklen = 144;
  if (unlikely(!jobj_binary(res_val, "data", work->data, worklen, true))) {
    if (opt_morenotices)
      applog(LOG_ERR, "%s: JSON inval data", isnull(get_pool_name(work->pool), ""));
    return false;
  }

  if (work->pool->algorithm.type == ALGO_CRE || work->pool->algorithm.type == ALGO_SCRYPT) {
    if (!jobj_binary(res_val, "midstate", work->midstate, sizeof(work->midstate), false)) {
      // Calculate it ourselves
      if (opt_morenotices) {
        applog(LOG_DEBUG, "%s: Calculating midstate locally", isnull(get_pool_name(work->pool), ""));
      }
      if (work->pool->algorithm.calc_midstate) work->pool->algorithm.calc_midstate(work);
    }
  }

  if (unlikely(!jobj_binary(res_val, "target", work->target, sizeof(work->target), true))) {
    if (opt_morenotices)
      applog(LOG_ERR, "%s: JSON inval target", isnull(get_pool_name(work->pool), ""));
    return false;
  }
  if (work->pool->algorithm.type == ALGO_DECRED) {
    uint16_t vote = (uint16_t) (opt_vote << 1) | 1;
    memcpy(&work->data[100], &vote, 2);
    // some random extradata to make it unique
    ((uint32_t*)work->data)[36] = (rand()*4);
    ((uint32_t*)work->data)[37] = (rand()*4) << 8 | work->thr_id;
  }
  return true;
}

/* Returns whether the pool supports local work generation or not. */
static bool pool_localgen(struct pool *pool)
{
  return (pool->has_stratum || pool->has_gbt);
}

static bool work_decode(struct pool *pool, struct work *work, json_t *val)
{
  json_t *res_val = json_object_get(val, "result");
  bool ret = false;

  cgtime(&pool->tv_lastwork);
  if (!res_val || json_is_null(res_val)) {
    applog(LOG_ERR, "JSON Failed to decode result");
    goto out;
  }

  work->pool = pool;

  if (pool->has_gbt) {
    if (unlikely(!gbt_decode(pool, res_val)))
      goto out;
    work->gbt = true;
    ret = true;
    goto out;
  } else if (unlikely(!getwork_decode(res_val, work)))
    goto out;

  memset(work->hash, 0, sizeof(work->hash));

  cgtime(&work->tv_staged);

  ret = true;

out:
  return ret;
}

static bool work_decode_eth(struct pool *pool, struct work *work, json_t *val)
{
  int i;
  bool ret = false;
  uint8_t SeedHash[32], Target[32];
  const char *EthWorkStr, *SeedHashStr, *TgtStr;

  cgtime(&pool->tv_lastwork);

  json_t *res_arr = json_object_get(val, "result");
  if (json_is_null(res_arr))
    return false;

  EthWorkStr = json_string_value(json_array_get(res_arr, 0));

  SeedHashStr = json_string_value(json_array_get(res_arr, 1));

  TgtStr = json_string_value(json_array_get(res_arr, 2));

  if (!eth_hex2bin(work->data, EthWorkStr, 32))
    goto out;

  if (!eth_hex2bin(SeedHash, SeedHashStr, 32))
    goto out;

  if (!eth_hex2bin(Target, TgtStr, 32))
    goto out;
  swab256(work->target, Target);

  cg_ilock(&pool->data_lock);
  if (pool->eth_cache.current_epoch == UINT32_MAX || memcmp(pool->eth_cache.seed_hash, SeedHash, 32)) {
    cg_ulock(&pool->data_lock);
    pool->eth_cache.current_epoch = EthCalcEpochNumber(SeedHash);
    memcpy(pool->eth_cache.seed_hash, SeedHash, 32);
    eth_gen_cache(pool);
    cg_dwlock(&pool->data_lock);
  }
  else
    cg_dlock(&pool->data_lock);
  mutex_lock(&eth_nonce_lock);
  work->Nonce = (uint64_t) eth_nonce++ << 32;
  mutex_unlock(&eth_nonce_lock);
  work->blk.nonce = 0;
  work->eth_epoch = pool->eth_cache.current_epoch;
  work->sdiff = le256todiff(work->target, work->pool->algorithm.diff_multiplier2);
  cg_runlock(&pool->data_lock);

  cgtime(&work->tv_staged);
  ret = true;

out:
  return ret;
}

#else /* HAVE_LIBCURL */
/* Always true with stratum */
#define pool_localgen(pool) (true)
#define json_rpc_call(curl, curl_err_str, url, userpass, rpc_req, probe, longpoll, rolltime, pool, share) (NULL)
#define work_decode(pool, work, val) (false)
#define gen_gbt_work(pool, work) {}
#endif /* HAVE_LIBCURL */

int dev_from_id(int thr_id)
{
  struct cgpu_info *cgpu = get_thr_cgpu(thr_id);

  return cgpu->device_id;
}

/* Create an exponentially decaying average over the opt_log_interval */
void decay_time(double *f, double fadd, double fsecs)
{
  double ftotal, fprop;

  fprop = 1.0 - 1 / (exp(fsecs / (double)opt_log_interval));
  ftotal = 1.0 + fprop;
  *f += (fadd * fprop);
  *f /= ftotal;
}

static int __total_staged(void)
{
  return HASH_COUNT(staged_work);
}

static int total_staged(void)
{
  int ret;

  mutex_lock(stgd_lock);
  ret = __total_staged();
  mutex_unlock(stgd_lock);

  return ret;
}

#ifdef HAVE_CURSES
WINDOW *mainwin, *statuswin, *logwin;
#endif
double total_secs = 1.0;
static char statusline[256];
/* logstart is where the log window should start */
static int devcursor, logstart, logcursor;
#ifdef HAVE_CURSES
/* statusy is where the status window goes up to in cases where it won't fit at startup */
static int statusy;
#endif

extern struct cgpu_info gpus[MAX_GPUDEVICES]; /* Maximum number apparently possible */

#ifdef HAVE_CURSES
static inline void unlock_curses(void)
{
  mutex_unlock(&console_lock);
}

static inline void lock_curses(void)
{
  mutex_lock(&console_lock);
}

static bool curses_active_locked(void)
{
  bool ret;

  lock_curses();
  ret = curses_active;
  if (!ret)
    unlock_curses();
  return ret;
}
#endif

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void suffix_string(uint64_t val, char *buf, size_t bufsiz, int sigdigits)
{
  const double  dkilo = 1000.0;
  const uint64_t kilo = 1000ull;
  const uint64_t mega = 1000000ull;
  const uint64_t giga = 1000000000ull;
  const uint64_t tera = 1000000000000ull;
  const uint64_t peta = 1000000000000000ull;
  const uint64_t exa  = 1000000000000000000ull;
  char suffix[2] = "";
  bool decimal = true;
  double dval;

  if (val >= exa) {
    val /= peta;
    dval = (double)val / dkilo;
    strcpy(suffix, "E");
  } else if (val >= peta) {
    val /= tera;
    dval = (double)val / dkilo;
    strcpy(suffix, "P");
  } else if (val >= tera) {
    val /= giga;
    dval = (double)val / dkilo;
    strcpy(suffix, "T");
  } else if (val >= giga) {
    val /= mega;
    dval = (double)val / dkilo;
    strcpy(suffix, "G");
  } else if (val >= mega) {
    val /= kilo;
    dval = (double)val / dkilo;
    strcpy(suffix, "M");
  } else if (val >= kilo) {
    dval = (double)val / dkilo;
    strcpy(suffix, "K");
  } else {
    dval = val;
    decimal = false;
  }

  if (!sigdigits) {
    if (decimal)
      snprintf(buf, bufsiz, "%.3g%s", dval, suffix);
    else
      snprintf(buf, bufsiz, "%d%s", (unsigned int)dval, suffix);
  } else {
    /* Always show sigdigits + 1, padded on right with zeroes
     * followed by suffix */
    int ndigits = sigdigits - 1 - (dval > 0.0 ? (int)floor(log10(dval)) : 0);

    snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
  }
}

/* Convert a double value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
void suffix_string_double(double val, char *buf, size_t bufsiz, int sigdigits)
{
  if (val < 10) {
    snprintf(buf, bufsiz, "%.3f", val);
  } else {
    return suffix_string(val, buf, bufsiz, sigdigits);
  }
}


double cgpu_runtime(struct cgpu_info *cgpu)
{
  struct timeval now;
  double dev_runtime;

  if (cgpu->dev_start_tv.tv_sec == 0)
    dev_runtime = total_secs;
  else {
    cgtime(&now);
    dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
  }

  if (dev_runtime < 1.0)
    dev_runtime = 1.0;
  return dev_runtime;
}

static void get_statline(char *buf, size_t bufsiz, struct cgpu_info *cgpu)
{
  char displayed_hashes[16], displayed_rolling[16];
  double dev_runtime, wu;
  uint64_t dh64, dr64;

  dev_runtime = cgpu_runtime(cgpu);

  wu = cgpu->diff1 / dev_runtime * 60.0;

  dh64 = (double)cgpu->total_mhashes / dev_runtime * 1000000ull;
  dr64 = (double)cgpu->rolling * 1000000ull;
  suffix_string(dh64, displayed_hashes, sizeof(displayed_hashes), 4);
  suffix_string(dr64, displayed_rolling, sizeof(displayed_rolling), 4);

  snprintf(buf, bufsiz, "%s%d ", cgpu->drv->name, cgpu->device_id);
  cgpu->drv->get_statline_before(buf, bufsiz, cgpu);
  tailsprintf(buf, bufsiz, "(%ds):%s (avg):%sh/s | A:%.0f R:%.0f HW:%d WU:%.3f/m",
    opt_log_interval,
    displayed_rolling,
    displayed_hashes,
    cgpu->diff_accepted,
    cgpu->diff_rejected,
    cgpu->hw_errors,
    wu);
  cgpu->drv->get_statline(buf, bufsiz, cgpu);
}

static bool shared_strategy(void)
{
  return (pool_strategy == POOL_LOADBALANCE || pool_strategy == POOL_BALANCE);
}

#ifdef HAVE_CURSES
#define CURBUFSIZ 256
#define cg_mvwprintw(win, y, x, fmt, ...) do { \
  char tmp42[CURBUFSIZ]; \
  snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
  mvwprintw(win, y, x, "%s", tmp42); \
} while (0)
#define cg_wprintw(win, fmt, ...) do { \
  char tmp42[CURBUFSIZ]; \
  snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
  wprintw(win, "%s", tmp42); \
} while (0)

/* Must be called with curses mutex lock held and curses_active */
static void curses_print_uptime(struct timeval *start_time)
{
  struct timeval now, tv;
  unsigned int days, hours;
  div_t d;

  cgtime(&now);
  timersub(&now, start_time, &tv);
  d = div((int)tv.tv_sec, 86400);
  days = d.quot;
  d = div(d.rem, 3600);
  hours = d.quot;
  d = div(d.rem, 60);
  cg_wprintw(statuswin, " - [%u day%c %02d:%02d:%02d]"
    , days
    , (days == 1) ? ' ' : 's'
    , hours
    , d.quot
    , d.rem
  );
}


/* Must be called with curses mutex lock held and curses_active */
static void curses_print_status(void)
{
  struct pool *pool = current_pool();
  unsigned short int line = 0;

  wattron(statuswin, A_BOLD);
  cg_mvwprintw(statuswin, line, 0, PACKAGE " " CGMINER_VERSION " - Started: %s", datestamp);
  curses_print_uptime(&launch_time);
  wattroff(statuswin, A_BOLD);

  mvwhline(statuswin, ++line, 0, '-', 80);

  cg_mvwprintw(statuswin, ++line, 0, "%s", statusline);
  wclrtoeol(statuswin);

  cg_mvwprintw(statuswin, ++line, 0, "ST: %d  SS: %d  NB: %d  LW: %d  GF: %d  RF: %d",
    total_staged(), total_stale, new_blocks,
    local_work, total_go, total_ro);
  wclrtoeol(statuswin);

  if (shared_strategy() && total_pools > 1) {
    cg_mvwprintw(statuswin, ++line, 0, "Connected to multiple pools %s block change notify",
           have_longpoll ? "with": "without");
  } else {
    cg_mvwprintw(statuswin, ++line, 0, "Connected to %s (%s) diff %s as user %s",
           get_pool_name(pool),
           pool->has_stratum ? "stratum" : (pool->has_gbt ? "GBT" : "longpoll"),
           pool->diff,
           get_pool_user(pool));
  }

  wclrtoeol(statuswin);

  cg_mvwprintw(statuswin, ++line, 0, "Block: %s...  Diff:%s  Started: %s  Best share: %s   ",
         prev_block, block_diff, blocktime, best_share);

  mvwhline(statuswin, ++line, 0, '-', 80);
  mvwhline(statuswin, statusy - 1, 0, '-', 80);

  cg_mvwprintw(statuswin, devcursor - 1, 0, "[P]ool management [G]PU management [S]ettings [D]isplay options [Q]uit");
}

static void adj_width(int var, int *length)
{
  if ((int)(log10((double)var) + 1) > *length)
    (*length)++;
}

static int dev_width;

static void curses_print_devstatus(struct cgpu_info *cgpu, int count)
{
  static int drwidth = 5, hwwidth = 1, wuwidth = 1;
  char logline[256];
  char displayed_hashes[16], displayed_rolling[16];
  float reject_pct = 0.0;
  uint64_t dh64, dr64;
  struct timeval now;
  double dev_runtime, wu;

  /* Do not print if window vertical size too small. */
  if (devcursor + count > LINES - 2)
    return;

  if (count >= (opt_removedisabled ? most_devices : total_devices))
    return;

  if (cgpu->dev_start_tv.tv_sec == 0)
    dev_runtime = total_secs;
  else {
    cgtime(&now);
    dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
  }

  if (dev_runtime < 1.0)
    dev_runtime = 1.0;

  cgpu->utility = cgpu->accepted / dev_runtime * 60;
  wu = cgpu->diff1 / dev_runtime * 60;

  wmove(statuswin, devcursor + count, 0);
  cg_wprintw(statuswin, "%s %*d: ", cgpu->drv->name, dev_width, cgpu->device_id);
  logline[0] = '\0';
  cgpu->drv->get_statline_before(logline, sizeof(logline), cgpu);
  cg_wprintw(statuswin, "%s", logline);

  dh64 = (double)cgpu->total_mhashes / dev_runtime * 1000000ull;
  dr64 = (double)cgpu->rolling * 1000000ull;
  suffix_string(dh64, displayed_hashes, sizeof(displayed_hashes), 4);
  suffix_string(dr64, displayed_rolling, sizeof(displayed_rolling), 4);

  if (cgpu->status == LIFE_DEAD)
    cg_wprintw(statuswin, "DEAD  ");
  else if (cgpu->status == LIFE_SICK)
    cg_wprintw(statuswin, "SICK  ");
  else if (cgpu->deven == DEV_DISABLED)
    cg_wprintw(statuswin, "OFF   ");
  else if (cgpu->deven == DEV_RECOVER)
    cg_wprintw(statuswin, "REST  ");
  else
    cg_wprintw(statuswin, "%6s", displayed_rolling);

  if ((cgpu->diff_accepted + cgpu->diff_rejected) > 0)
    reject_pct = (cgpu->diff_rejected / (cgpu->diff_accepted + cgpu->diff_rejected)) * 100;

  adj_width(cgpu->hw_errors, &hwwidth);
  adj_width(wu, &wuwidth);

  cg_wprintw(statuswin, "/%6sh/s | R:%*.1f%% HW:%*d WU:%*.3f/m",
      displayed_hashes,
      drwidth, reject_pct,
      hwwidth, cgpu->hw_errors,
      wuwidth + 2, wu);
  logline[0] = '\0';
  cgpu->drv->get_statline(logline, sizeof(logline), cgpu);
  cg_wprintw(statuswin, "%s", logline);

  wclrtoeol(statuswin);
}
#endif

#ifdef HAVE_CURSES
/* Check for window resize. Called with curses mutex locked */
static inline void change_logwinsize(void)
{
  int x, y, logx, logy;

  getmaxyx(mainwin, y, x);
  if (x < 80 || y < 25)
    return;

  if (y > statusy + 2 && statusy < logstart) {
    if (y - 2 < logstart)
      statusy = y - 2;
    else
      statusy = logstart;
    logcursor = statusy + 1;
    mvwin(logwin, logcursor, 0);
    wresize(statuswin, statusy, x);
  }

  y -= logcursor;
  getmaxyx(logwin, logy, logx);
  /* Detect screen size change */
  if (x != logx || y != logy)
    wresize(logwin, y, x);
}

static void check_winsizes(void)
{
  if (!use_curses)
    return;
  if (curses_active_locked()) {
    int y, x;

    erase();
    x = getmaxx(statuswin);
    if (logstart > LINES - 2)
      statusy = LINES - 2;
    else
      statusy = logstart;
    logcursor = statusy;
    wresize(statuswin, statusy, x);
    getmaxyx(mainwin, y, x);
    y -= logcursor;
    wresize(logwin, y, x);
    mvwin(logwin, logcursor, 0);
    unlock_curses();
  }
}

static void disable_curses_windows(void);
static void enable_curses_windows(void);

static void switch_logsize(bool __maybe_unused newdevs)
{
  if (curses_active_locked()) {
#ifdef WIN32
    if (newdevs)
      disable_curses_windows();
#endif
    if (opt_compact) {
      logstart = devcursor + 1;
    } else {
      logstart = devcursor + (opt_removedisabled ? most_devices : total_devices) + 1;
    }
    logcursor = logstart + 1;
#ifdef WIN32
    if (newdevs)
      enable_curses_windows();
#endif
    unlock_curses();
    check_winsizes();
  }
}

/* For mandatory printing when mutex is already locked */
void _wlog(const char *str)
{
  wprintw(logwin, "%s", str);
}

/* Mandatory printing */
void _wlogprint(const char *str)
{
  if (curses_active_locked()) {
    wprintw(logwin, "%s", str);
    unlock_curses();
  }
}
#else
static void switch_logsize(bool __maybe_unused newdevs)
{
}
#endif

#ifdef HAVE_CURSES
bool _log_curses_only(int prio, const char *datetime, const char *str)
{
  bool high_prio;

  high_prio = (prio == LOG_WARNING || prio == LOG_ERR);

  if (curses_active) {
    if (!opt_loginput || high_prio) {
      wprintw(logwin, "%s%s\n", datetime, str);
      if (high_prio) {
        touchwin(logwin);
        wrefresh(logwin);
      }
    }
    return true;
  }
  return false;
}

void clear_logwin(void)
{
  if (curses_active_locked()) {
    erase();
    wclear(logwin);
    unlock_curses();
  }
}

void logwin_update(void)
{
  if (curses_active_locked()) {
    touchwin(logwin);
    wrefresh(logwin);
    unlock_curses();
  }
}
#endif

static void restart_threads(void);

/* Theoretically threads could race when modifying accepted and
 * rejected values but the chance of two submits completing at the
 * same time is zero so there is no point adding extra locking */
static void
share_result(json_t *val, json_t *res, json_t *err, const struct work *work,
       char *hashshow, bool resubmit, char *worktime)
{
  struct pool *pool = work->pool;
  struct cgpu_info *cgpu;

  cgpu = get_thr_cgpu(work->thr_id);

  if (json_is_true(res) || (work->gbt && json_is_null(res))) {
    mutex_lock(&stats_lock);
    cgpu->accepted++;
    total_accepted++;
    pool->accepted++;
    cgpu->diff_accepted += work->work_difficulty;
    total_diff_accepted += work->work_difficulty;
    pool->diff_accepted += work->work_difficulty;
    mutex_unlock(&stats_lock);

    pool->seq_rejects = 0;
    cgpu->last_share_pool = pool->pool_no;
    cgpu->last_share_pool_time = time(NULL);
    cgpu->last_share_diff = work->work_difficulty;
    pool->last_share_time = cgpu->last_share_pool_time;
    pool->last_share_diff = work->work_difficulty;
    applog(LOG_DEBUG, "[THR%d] PROOF OF WORK RESULT: true (yay!!!)", work->thr_id);
    if (!QUIET) {
      if (total_pools > 1) {
        applog(LOG_NOTICE, "Accepted %s %s %d at %s %s%s",
               hashshow,
               cgpu->drv->name,
               cgpu->device_id,
               get_pool_name(pool),
               resubmit ? "(resubmit)" : "", worktime);
      } else {
        applog(LOG_DEBUG, "[THR%d] Accepted %s %s %d %s%s",
               work->thr_id, hashshow, cgpu->drv->name, cgpu->device_id, resubmit ? "(resubmit)" : "", worktime);
        applog(LOG_NOTICE, "Accepted %s %s %d %s%s",
               hashshow, cgpu->drv->name, cgpu->device_id, resubmit ? "(resubmit)" : "", worktime);
      }
    }
    sharelog("accept", work);
    if (opt_shares && total_diff_accepted >= opt_shares) {
      applog(LOG_WARNING, "Successfully mined %d accepted shares as requested and exiting.", opt_shares);
      kill_work();
      return;
    }

    /* Detect if a pool that has been temporarily disabled for
     * continually rejecting shares has started accepting shares.
     * This will only happen with the work returned from a
     * longpoll */
    if (unlikely(pool->state == POOL_REJECTING)) {
      applog(LOG_WARNING, "Rejecting %s now accepting shares, re-enabling!",
             get_pool_name(pool));
      enable_pool(pool);
      switch_pools(NULL);
    }
    /* If we know we found the block we know better than anyone
     * that new work is needed. */
    if (unlikely(work->block))
      restart_threads();
  } else {
    mutex_lock(&stats_lock);
    cgpu->rejected++;
    total_rejected++;
    pool->rejected++;
    cgpu->diff_rejected += work->work_difficulty;
    total_diff_rejected += work->work_difficulty;
    pool->diff_rejected += work->work_difficulty;
    pool->seq_rejects++;
    mutex_unlock(&stats_lock);

    applog(LOG_DEBUG, "[THR%d] PROOF OF WORK RESULT: false (booooo)", work->thr_id);
    if (!QUIET) {
      char disposition[36] = "reject";
      char reason[32];

      strcpy(reason, "");

      if (!work->gbt)
        res = json_object_get(val, "reject-reason");
      if (res) {
        const char *reasontmp = json_string_value(res);

        size_t reasonLen = strlen(reasontmp);
        if (reasonLen > 28)
          reasonLen = 28;
        reason[0] = ' '; reason[1] = '(';
        memcpy(2 + reason, reasontmp, reasonLen);
        reason[reasonLen + 2] = ')'; reason[reasonLen + 3] = '\0';
        memcpy(disposition + 7, reasontmp, reasonLen);
        disposition[6] = ':'; disposition[reasonLen + 7] = '\0';
      } else if (work->stratum && err && json_is_array(err)) {
        json_t *reason_val = json_array_get(err, 1);
        char *reason_str;

        if (reason_val && json_is_string(reason_val)) {
          reason_str = (char *)json_string_value(reason_val);
          snprintf(reason, 31, " (%s)", reason_str);
        }
      }

      applog(LOG_NOTICE, "Rejected %s %s %d %s%s %s%s",
             hashshow,
             cgpu->drv->name,
             cgpu->device_id,
             (total_pools > 1) ? get_pool_name(pool) : "",
             reason, resubmit ? "(resubmit)" : "",
             worktime);
      sharelog(disposition, work);
    }

    /* Once we have more than a nominal amount of sequential rejects,
     * at least 10 and more than 3 mins at the current utility,
     * disable the pool because some pool error is likely to have
     * ensued. Do not do this if we know the share just happened to
     * be stale due to networking delays.
     */
    if (pool->seq_rejects > 10 && !work->stale && opt_disable_pool && enabled_pools > 1) {
      double utility = total_accepted / total_secs * 60;

      if (pool->seq_rejects > utility * 3) {
        applog(LOG_WARNING, "%s rejected %d sequential shares, disabling!",
               get_pool_name(pool),
               pool->seq_rejects);
        reject_pool(pool);
        if (pool == current_pool())
          switch_pools(NULL);
        pool->seq_rejects = 0;
      }
    }
  }
}

static void show_hash(struct work *work, char *hashshow)
{
  unsigned char rhash[32];
  char diffdisp[16], wdiffdisp[16];
  unsigned long h32;
  uint32_t *hash32;
  int ofs;

  suffix_string_double(work->share_diff, diffdisp, sizeof (diffdisp), 0);
  suffix_string_double(work->work_difficulty, wdiffdisp, sizeof (wdiffdisp), 0);
  if (opt_show_coindiff) {
    snprintf(hashshow, 64, "Coin %.0f Diff %s/%s%s", get_work_blockdiff(work), diffdisp, wdiffdisp,
     work->block? " BLOCK!" : "");
  } else {
    swab256(rhash, work->hash);
    for (ofs = 0; ofs <= 28; ++ofs) {
      if (rhash[ofs]) {
        break;
      }
    }
    hash32 = (uint32_t *)(rhash + ofs);
    h32 = be32toh(*hash32);

    snprintf(hashshow, 64, "%08lx Diff %s/%s%s", h32, diffdisp, wdiffdisp,
       work->block ? " BLOCK!" : "");
  }
}

#ifdef HAVE_LIBCURL
static void text_print_status(int thr_id)
{
  struct cgpu_info *cgpu;
  char logline[256];

  cgpu = get_thr_cgpu(thr_id);
  if (cgpu) {
    get_statline(logline, sizeof(logline), cgpu);
    printf("%s\n", logline);
  }
}

static void print_status(int thr_id)
{
  if (!curses_active)
    text_print_status(thr_id);
}

static bool submit_upstream_work(struct work *work, CURL *curl, char *curl_err_str, bool resubmit)
{
  char *hexstr = NULL;
  json_t *val, *res, *err;
  char *s;
  bool rc = false;
  int thr_id = work->thr_id;
  struct cgpu_info *cgpu;
  struct pool *pool = work->pool;
  int rolltime;
  struct timeval tv_submit, tv_submit_reply;
  char hashshow[64 + 4] = "";
  char worktime[200] = "";
  struct timeval now;
  double dev_runtime;

  cgpu = get_thr_cgpu(thr_id);

  if(work->pool->algorithm.type == ALGO_ETHASH) {
    s = (char *)malloc(sizeof(char) * (128 + 16 + 512));
    uint64_t tmp = htobe64(work->Nonce);
    char *ASCIIMixHash = bin2hex(work->mixhash, 32);
    char *ASCIIPoWHash = bin2hex(work->data, 32);
    char *ASCIINonce = bin2hex((uint8_t*) &tmp, 8);

    snprintf(s, 128 + 16 + 512, "{\"jsonrpc\":\"2.0\", \"method\":\"eth_submitWork\", \"params\":[\"0x%s\", \"0x%s\", \"0x%s\"],\"id\":1}", ASCIINonce, ASCIIPoWHash, ASCIIMixHash);

    free(ASCIINonce);
    free(ASCIIMixHash);
    free(ASCIIPoWHash);
  } else {
    if (work->pool->algorithm.type == ALGO_DECRED) {
      endian_flip180(work->data, work->data);
    } else if (work->pool->algorithm.type == ALGO_CRE) {
      endian_flip168(work->data, work->data);
    } else {
      endian_flip128(work->data, work->data);
    }

    /* build hex string - Make sure to restrict to 80 bytes for Neoscrypt */
    int datasize = 128;
    if (work->pool->algorithm.type == ALGO_NEOSCRYPT || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
        work->pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI) datasize = 80;
    else if (work->pool->algorithm.type == ALGO_CRE) datasize = 168;
    else if (work->pool->algorithm.type == ALGO_DECRED) {
      datasize = 192;
      ((uint32_t*)work->data)[45] = 0x80000001UL;
      ((uint32_t*)work->data)[46] = 0;
      ((uint32_t*)work->data)[47] = 0x000005a0UL;
    }

    hexstr = bin2hex(work->data, datasize);

    /* build JSON-RPC request */
    if (work->gbt) {
      char *gbt_block, *varint;
      unsigned char data[80];

      flip80(data, work->data);
      gbt_block = bin2hex(data, 80);

      if (work->gbt_txns < 0xfd) {
        uint8_t val = work->gbt_txns;

        varint = bin2hex((const unsigned char *)&val, 1);
      } else if (work->gbt_txns <= 0xffff) {
        uint16_t val = htole16(work->gbt_txns);

        gbt_block = (char *)realloc_strcat(gbt_block, "fd");
        varint = bin2hex((const unsigned char *)&val, 2);
      } else {
        uint32_t val = htole32(work->gbt_txns);

        gbt_block = (char *)realloc_strcat(gbt_block, "fe");
        varint = bin2hex((const unsigned char *)&val, 4);
      }
      gbt_block = (char *)realloc_strcat(gbt_block, varint);
      free(varint);
      gbt_block = (char *)realloc_strcat(gbt_block, work->coinbase);

      s = strdup("{\"id\": 0, \"method\": \"submitblock\", \"params\": [\"");
      s = (char *)realloc_strcat(s, gbt_block);
      if (work->job_id) {
        s = (char *)realloc_strcat(s, "\", {\"workid\": \"");
        s = (char *)realloc_strcat(s, work->job_id);
        s = (char *)realloc_strcat(s, "\"}]}");
      } else
        s = (char *)realloc_strcat(s, "\", {}]}");
      free(gbt_block);
    } else {
      s = strdup("{\"method\": \"getwork\", \"params\": [ \"");
      s = (char *)realloc_strcat(s, hexstr);
      s = (char *)realloc_strcat(s, "\" ], \"id\":1}");
    }
  }

  applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->rpc_url, s);
  s = (char *)realloc_strcat(s, "\n");

  cgtime(&tv_submit);
  /* issue JSON-RPC request */
  val = json_rpc_call(curl, curl_err_str, pool->rpc_url, pool->rpc_userpass, s, false, false, &rolltime, pool, true);
  cgtime(&tv_submit_reply);
  free(s);

  if (unlikely(!val)) {
    applog(LOG_INFO, "submit_upstream_work json_rpc_call failed");
    if (!pool_tset(pool, &pool->submit_fail)) {
      total_ro++;
      pool->remotefail_occasions++;
      if (opt_lowmem) {
        applog(LOG_WARNING, "%s communication failure, discarding shares", get_pool_name(pool));
        goto out;
      }
      applog(LOG_WARNING, "%s communication failure, caching submissions", get_pool_name(pool));
    }
    cgsleep_ms(5000);
    goto out;
  } else if (pool_tclear(pool, &pool->submit_fail))
    applog(LOG_WARNING, "%s communication resumed, submitting work", get_pool_name(pool));

  res = json_object_get(val, "result");
  err = json_object_get(val, "error");

  if (!QUIET) {
    show_hash(work, hashshow);

    if (opt_worktime) {
      char workclone[20];
      struct tm *tm, tm_getwork, tm_submit_reply;
      double getwork_time = tdiff((struct timeval *)&(work->tv_getwork_reply),
              (struct timeval *)&(work->tv_getwork));
      double getwork_to_work = tdiff((struct timeval *)&(work->tv_work_start),
              (struct timeval *)&(work->tv_getwork_reply));
      double work_time = tdiff((struct timeval *)&(work->tv_work_found),
              (struct timeval *)&(work->tv_work_start));
      double work_to_submit = tdiff(&tv_submit,
              (struct timeval *)&(work->tv_work_found));
      double submit_time = tdiff(&tv_submit_reply, &tv_submit);
      int diffplaces = 3;

      time_t tmp_time = work->tv_getwork.tv_sec;
      tm = localtime(&tmp_time);
      memcpy(&tm_getwork, tm, sizeof(struct tm));
      tmp_time = tv_submit_reply.tv_sec;
      tm = localtime(&tmp_time);
      memcpy(&tm_submit_reply, tm, sizeof(struct tm));

      if (work->clone) {
        snprintf(workclone, sizeof(workclone), "C:%1.3f",
            tdiff((struct timeval *)&(work->tv_cloned),
            (struct timeval *)&(work->tv_getwork_reply)));
      }
      else
        strcpy(workclone, "O");

      if (work->work_difficulty < 1)
        diffplaces = 6;

      snprintf(worktime, sizeof(worktime),
        " <-%08lx.%08lx M:%c D:%1.*f G:%02d:%02d:%02d:%1.3f %s (%1.3f) W:%1.3f (%1.3f) S:%1.3f R:%02d:%02d:%02d",
        (unsigned long)be32toh(*(uint32_t *)&(work->data[32])),
        (unsigned long)be32toh(*(uint32_t *)&(work->data[28])),
        work->getwork_mode, diffplaces, work->work_difficulty,
        tm_getwork.tm_hour, tm_getwork.tm_min,
        tm_getwork.tm_sec, getwork_time, workclone,
        getwork_to_work, work_time, work_to_submit, submit_time,
        tm_submit_reply.tm_hour, tm_submit_reply.tm_min,
        tm_submit_reply.tm_sec);
    }
  }

  share_result(val, res, err, work, hashshow, resubmit, worktime);

  if (cgpu->dev_start_tv.tv_sec == 0)
    dev_runtime = total_secs;
  else {
    cgtime(&now);
    dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
  }

  if (dev_runtime < 1.0)
    dev_runtime = 1.0;

  cgpu->utility = cgpu->accepted / dev_runtime * 60;

  if (!opt_realquiet)
    print_status(thr_id);
  if (!want_per_device_stats) {
    char logline[256];

    get_statline(logline, sizeof(logline), cgpu);
    applog(LOG_INFO, "%s", logline);
  }

  json_decref(val);

  rc = true;
out:
  free(hexstr);
  return rc;
}

char eth_getwork_rpc[] = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getWork\",\"params\":[],\"id\":1}";
char eth_gethighestblock_rpc[] = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"latest\", false],\"id\":1}";

static bool get_upstream_work(struct work *work, CURL *curl, char *curl_err_str)
{
  struct pool *pool = work->pool;
  struct sgminer_pool_stats *pool_stats = &(pool->sgminer_pool_stats);
  struct timeval tv_elapsed;
  json_t *val = NULL;
  bool rc = false;
  char *url;

  applog(LOG_DEBUG, "DBG: sending %s get RPC call: %s", pool->rpc_url, pool->rpc_req);

  url = pool->rpc_url;

  cgtime(&work->tv_getwork);

  if(pool->algorithm.type == ALGO_ETHASH) {
    pool->rpc_req = eth_getwork_rpc;

    val = json_rpc_call(curl, curl_err_str, url, pool->rpc_userpass,
          pool->rpc_req, false, false, &work->rolltime, pool, false);
  } else {
    val = json_rpc_call(curl, curl_err_str, url, pool->rpc_userpass, pool->rpc_req, false,
            false, &work->rolltime, pool, false);
    pool_stats->getwork_attempts++;
  }

  if (likely(val)) {
    rc = (pool->algorithm.type == ALGO_ETHASH) ? work_decode_eth(pool, work, val) : work_decode(pool, work, val);
    if (unlikely(!rc))
      applog(LOG_DEBUG, "Failed to decode work in get_upstream_work");
  } else
    applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");

  cgtime(&work->tv_getwork_reply);
  timersub(&(work->tv_getwork_reply), &(work->tv_getwork), &tv_elapsed);
  pool_stats->getwork_wait_rolling += ((double)tv_elapsed.tv_sec + ((double)tv_elapsed.tv_usec / 1000000)) * 0.63;
  pool_stats->getwork_wait_rolling /= 1.63;

  timeradd(&tv_elapsed, &(pool_stats->getwork_wait), &(pool_stats->getwork_wait));
  if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_max), >)) {
    pool_stats->getwork_wait_max.tv_sec = tv_elapsed.tv_sec;
    pool_stats->getwork_wait_max.tv_usec = tv_elapsed.tv_usec;
  }
  if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_min), <)) {
    pool_stats->getwork_wait_min.tv_sec = tv_elapsed.tv_sec;
    pool_stats->getwork_wait_min.tv_usec = tv_elapsed.tv_usec;
  }
  pool_stats->getwork_calls++;

  work->pool = pool;
  work->longpoll = false;
  work->getwork_mode = GETWORK_MODE_POOL;
  calc_diff(work, 0);
  total_getworks++;
  pool->getwork_requested++;

  if (likely(val))
    json_decref(val);

  return rc;
}
#endif /* HAVE_LIBCURL */

/* Specifies whether we can use this pool for work or not. */
static bool pool_unworkable(struct pool *pool)
{
  if (pool->idle)
    return true;
  if (pool->state != POOL_ENABLED)
    return true;
  if (pool->has_stratum && !pool->stratum_active)
    return true;
  return false;
}

/* In balanced mode, the amount of diff1 solutions per pool is monitored as a
 * rolling average per 10 minutes and if pools start getting more, it biases
 * away from them to distribute work evenly. The share count is reset to the
 * rolling average every 10 minutes to not send all work to one pool after it
 * has been disabled/out for an extended period. */
static struct pool *select_balanced(struct pool *cp)
{
  int i, lowest = cp->shares;
  struct pool *ret = cp;

  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];

    if (pool_unworkable(pool))
      continue;
    if (pool->shares < lowest) {
      lowest = pool->shares;
      ret = pool;
    }
  }

  ret->shares++;
  return ret;
}

static struct pool *priority_pool(int choice);
static bool pool_unusable(struct pool *pool);

/* Select any active pool in a rotating fashion when loadbalance is chosen if
 * it has any quota left. */
static inline struct pool *select_pool(bool lagging)
{
  static int rotating_pool = 0;
  struct pool *pool, *cp;
  bool avail = false;
  int tested, i;

  cp = current_pool();

  if (pool_strategy == POOL_BALANCE) {
    pool = select_balanced(cp);
    goto out;
  }

  if (pool_strategy != POOL_LOADBALANCE && (!lagging || opt_fail_only)) {
    pool = cp;
    goto out;
  } else
    pool = NULL;

  for (i = 0; i < total_pools; i++) {
    struct pool *tp = pools[i];

    if (tp->quota_used < tp->quota_gcd) {
      avail = true;
      break;
    }
  }

  /* There are no pools with quota, so reset them. */
  if (!avail) {
    for (i = 0; i < total_pools; i++)
      pools[i]->quota_used = 0;
    if (++rotating_pool >= total_pools)
      rotating_pool = 0;
  }

  /* Try to find the first pool in the rotation that is usable */
  tested = 0;
  while (!pool && tested++ < total_pools) {
    pool = pools[rotating_pool];
    if (pool->quota_used++ < pool->quota_gcd) {
      if (!pool_unworkable(pool))
        break;
      /* Failover-only flag for load-balance means distribute
       * unused quota to priority pool 0. */
      if (opt_fail_only)
        priority_pool(0)->quota_used--;
    }
    pool = NULL;
    if (++rotating_pool >= total_pools)
      rotating_pool = 0;
  }

  /* If there are no alive pools with quota, choose according to
   * priority. */
  if (!pool) {
    for (i = 0; i < total_pools; i++) {
      struct pool *tp = priority_pool(i);

      if (!pool_unusable(tp)) {
        pool = tp;
        break;
      }
    }
  }

  /* If still nothing is usable, use the current pool */
  if (!pool)
    pool = cp;
out:
  applog(LOG_DEBUG, "Selecting %s for work", get_pool_name(pool));
  return pool;
}

/*
 * Calculate the work->work_difficulty based on the work->target
 */
static void calc_diff(struct work *work, double known)
{
  struct sgminer_pool_stats *pool_stats = &(work->pool->sgminer_pool_stats);
  double difficulty;

  if (known) {
    work->work_difficulty = known;
  } else {
    double d64, dcut64;

    d64 = work->pool->algorithm.diff_multiplier2 * truediffone;

    applog(LOG_DEBUG, "calc_diff() algorithm = %s", work->pool->algorithm.name);
    // Neoscrypt
    if (work->pool->algorithm.type == ALGO_NEOSCRYPT || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
        work->pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI ||
        work->pool->algorithm.type == ALGO_YESCRYPTR16 || work->pool->algorithm.type == ALGO_YESCRYPTR16_NAVI) {
      dcut64 = (double)*((uint64_t *)(work->target + 22));
    }
    else {
      dcut64 = le256todouble(work->target);
    }
    if (unlikely(!dcut64))
      dcut64 = 1;
    work->work_difficulty = d64 / dcut64;
    applog(LOG_DEBUG, "Difficulty: %f", work->work_difficulty);
  }

  difficulty = work->work_difficulty;

  pool_stats->last_diff = difficulty;
  suffix_string_double(difficulty, work->pool->diff, sizeof(work->pool->diff), 0);

  if (difficulty == pool_stats->min_diff)
    pool_stats->min_diff_count++;
  else if (difficulty < pool_stats->min_diff || pool_stats->min_diff == 0) {
    pool_stats->min_diff = difficulty;
    pool_stats->min_diff_count = 1;
  }

  if (difficulty == pool_stats->max_diff)
    pool_stats->max_diff_count++;
  else if (difficulty > pool_stats->max_diff) {
    pool_stats->max_diff = difficulty;
    pool_stats->max_diff_count = 1;
  }
}

#ifdef HAVE_CURSES
static void disable_curses_windows(void)
{
  leaveok(logwin, false);
  leaveok(statuswin, false);
  leaveok(mainwin, false);
  nocbreak();
  echo();
  delwin(logwin);
  delwin(statuswin);
}

/* Force locking of curses console_lock on shutdown since a dead thread might
 * have grabbed the lock. */
static bool curses_active_forcelocked(void)
{
  bool ret;

  mutex_trylock(&console_lock);
  ret = curses_active;
  if (!ret)
    unlock_curses();
  return ret;
}

static void disable_curses(void)
{
  if (curses_active_forcelocked()) {
    use_curses = false;
    curses_active = false;
    disable_curses_windows();
    delwin(mainwin);
    endwin();
#ifdef WIN32
    // Move the cursor to after curses output.
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD coord;

    if (GetConsoleScreenBufferInfo(hout, &csbi)) {
      coord.X = 0;
      coord.Y = csbi.dwSize.Y - 1;
      SetConsoleCursorPosition(hout, coord);
    }
#endif
    unlock_curses();
  }
}
#endif

static void kill_timeout(struct thr_info *thr)
{
  cg_completion_timeout(&thr_info_cancel_join, thr, 1000);
}

static void kill_mining(void)
{
  struct thr_info *thr;
  int i;

  forcelog(LOG_DEBUG, "Killing off mining threads");
  /* Kill the mining threads*/
  rd_lock(&mining_thr_lock);
  for (i = 0; i < mining_threads; i++) {
    thr = mining_thr[i];
    forcelog(LOG_DEBUG, "Waiting for thread %d to finish...", thr->id);
    thr_info_cancel_join(thr);
  }
  rd_unlock(&mining_thr_lock);
}

static void __kill_work(void)
{
  struct thr_info *thr;
  int i;

  if (!successful_connect)
    return;

  forcelog(LOG_INFO, "Received kill message");

  forcelog(LOG_DEBUG, "Killing off watchpool thread");
  /* Kill the watchpool thread */
  thr = &control_thr[watchpool_thr_id];
  kill_timeout(thr);

  forcelog(LOG_DEBUG, "Killing off watchdog thread");
  /* Kill the watchdog thread */
  thr = &control_thr[watchdog_thr_id];
  kill_timeout(thr);

  forcelog(LOG_DEBUG, "Shutting down mining threads");
  rd_lock(&mining_thr_lock);
  for (i = 0; i < mining_threads; i++) {
    struct cgpu_info *cgpu;

    thr = mining_thr[i];
    if (!thr)
      continue;
    cgpu = thr->cgpu;
    if (!cgpu)
      continue;

    cgpu->shutdown = true;
  }
  rd_unlock(&mining_thr_lock);

  sleep(1);

  cg_completion_timeout(&kill_mining, NULL, 3000);

  /* Stop the others */
  forcelog(LOG_DEBUG, "Killing off API thread");
  thr = &control_thr[api_thr_id];
  kill_timeout(thr);
}

/* This should be the common exit path */
void kill_work(void)
{
  __kill_work();

  quit(0, "Shutdown signal received.");
}

static
#ifdef WIN32
const
#endif
const char **initial_args;

static void clean_up(bool restarting);

void app_restart(void)
{
  applog(LOG_WARNING, "Attempting to restart %s", packagename);

  __kill_work();
  clean_up(true);

#if defined(unix) || defined(__APPLE__)
  if (forkpid > 0) {
    kill(forkpid, SIGTERM);
    forkpid = 0;
  }
#endif

  execv(initial_args[0], (EXECV_2ND_ARG_TYPE)initial_args);
  applog(LOG_WARNING, "Failed to restart application");
}

static void sighandler(int __maybe_unused sig)
{
#ifndef _MSC_VER
  /* Restore signal handlers so we can still quit if kill_work fails */
  sigaction(SIGTERM, &termhandler, NULL);
  sigaction(SIGINT, &inthandler, NULL);
#endif
  kill_work();
}

#ifdef HAVE_LIBCURL
/* Called with pool_lock held. Recruit an extra curl if none are available for
 * this pool. */
static void recruit_curl(struct pool *pool)
{
  struct curl_ent *ce = (struct curl_ent *)calloc(sizeof(struct curl_ent), 1);

  if (unlikely(!ce))
    quit(1, "Failed to calloc in recruit_curl");

  ce->curl = curl_easy_init();
  if (unlikely(!ce->curl))
    quit(1, "Failed to init in recruit_curl");

  list_add(&ce->node, &pool->curlring);
  pool->curls++;
}

/* Grab an available curl if there is one. If not, then recruit extra curls
 * unless we are in a submit_fail situation, or we have opt_delaynet enabled
 * and there are already 5 curls in circulation. Limit total number to the
 * number of mining threads per pool as well to prevent blasting a pool during
 * network delays/outages. */
static struct curl_ent *pop_curl_entry(struct pool *pool)
{
  int curl_limit = opt_delaynet ? 5 : (mining_threads + opt_queue) * 2;
  bool recruited = false;
  struct curl_ent *ce;

  mutex_lock(&pool->pool_lock);
retry:
  if (!pool->curls) {
    recruit_curl(pool);
    recruited = true;
  } else if (list_empty(&pool->curlring)) {
    if (pool->curls >= curl_limit) {
      pthread_cond_wait(&pool->cr_cond, &pool->pool_lock);
      goto retry;
    } else {
      recruit_curl(pool);
      recruited = true;
    }
  }
  ce = list_entry(pool->curlring.next, struct curl_ent*, node);

  list_del(&ce->node);
  mutex_unlock(&pool->pool_lock);

  if (recruited)
    applog(LOG_DEBUG, "Recruited curl for %s", get_pool_name(pool));
  return ce;
}

static void push_curl_entry(struct curl_ent *ce, struct pool *pool)
{
  mutex_lock(&pool->pool_lock);
  list_add_tail(&ce->node, &pool->curlring);
  cgtime(&ce->tv);
  pthread_cond_broadcast(&pool->cr_cond);
  mutex_unlock(&pool->pool_lock);
}

static bool stale_work(struct work *work, bool share);

static inline bool should_roll(struct work *work)
{
  struct timeval now;
  time_t expiry;

  if (work->pool != current_pool() && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE)
    return false;

  if (work->rolltime > opt_scantime)
    expiry = work->rolltime;
  else
    expiry = opt_scantime;
  expiry = expiry * 2 / 3;

  /* We shouldn't roll if we're unlikely to get one shares' duration
   * work out of doing so */
  cgtime(&now);
  if (now.tv_sec - work->tv_staged.tv_sec > expiry)
    return false;

  return true;
}

/* Limit rolls to 7000 to not beyond 2 hours in the future where bitcoind will
 * reject blocks as invalid. */
static inline bool can_roll(struct work *work)
{
  return (!work->stratum && work->pool && work->rolltime && !work->clone &&
    work->rolls < 7000 && !stale_work(work, false));
}

static uint32_t _get_work_time(struct work *work)
{
  uint32_t *data = (uint32_t*) work->data;
  uint32_t work_ntime = data[17];
  if (work->pool && work->pool->algorithm.type == ALGO_DECRED) {
    work_ntime = data[34];
  }
  return work_ntime;
}

static void _set_work_time(struct work *work, uint32_t ntime)
{
  uint32_t *data = (uint32_t*) work->data;
  uint32_t *work_ntime = &data[17];
  if (work->pool && work->pool->algorithm.type == ALGO_DECRED) {
    work_ntime = &data[34];
  }
  (*work_ntime) = ntime;
}

static void roll_work(struct work *work)
{
  uint32_t work_ntime;
  uint32_t ntime;

  work_ntime = _get_work_time(work);
  ntime = be32toh(work_ntime);
  ntime++;

  if (work->pool->algorithm.type == ALGO_DECRED) {
    uint32_t* data = (uint32_t*) work->data;
    // dont mess with ntime, use extranonce
    data[36]++;
    data[37] = ((rand()*4) << 8) | work->thr_id;
  } else {
    _set_work_time(work, htobe32(ntime));
  }

  local_work++;
  work->rolls++;
  work->blk.nonce = 0;
  applog(LOG_DEBUG, "Successfully rolled work");

  /* This is now a different work item so it needs a different ID for the
   * hashtable */
  work->id = total_work++;
}

static void *submit_work_thread(void *userdata)
{
  struct work *work = (struct work *)userdata;
  struct pool *pool = work->pool;
  bool resubmit = false;
  struct curl_ent *ce;

  pthread_detach(pthread_self());

  RenameThread("SubmitWork");

  applog(LOG_DEBUG, "Creating extra submit work thread");

  ce = pop_curl_entry(pool);
  /* submit solution to bitcoin via JSON-RPC */
  while (!submit_upstream_work(work, ce->curl, ce->curl_err_str, resubmit)) {
    if (opt_lowmem) {
      applog(LOG_NOTICE, "%s share being discarded to minimise memory cache", get_pool_name(pool));
      break;
    }
    resubmit = true;
    if (stale_work(work, true)) {
      applog(LOG_NOTICE, "%s share became stale while retrying submit, discarding", get_pool_name(pool));

      mutex_lock(&stats_lock);
      total_stale++;
      pool->stale_shares++;
      total_diff_stale += work->work_difficulty;
      pool->diff_stale += work->work_difficulty;
      mutex_unlock(&stats_lock);

      free_work(work);
      break;
    }

    /* pause, then restart work-request loop */
    applog(LOG_INFO, "json_rpc_call failed on submit_work, retrying");
  }
  push_curl_entry(ce, pool);

  return NULL;
}

static struct work *make_clone(struct work *work)
{
  struct work *work_clone = copy_work(work);

  if (work->pool->algorithm.type == ALGO_DECRED) {
    // maybe not useful here
    ((uint32_t*)work->data)[36] = (rand()*4);
    ((uint32_t*)work->data)[37] = (rand()*4) << 8;
  }

  work_clone->clone = true;
  cgtime((struct timeval *)&(work_clone->tv_cloned));
  work_clone->longpoll = false;
  work_clone->mandatory = false;
  /* Make cloned work appear slightly older to bias towards keeping the
   * master work item which can be further rolled */
  work_clone->tv_staged.tv_sec -= 1;

  return work_clone;
}

static void stage_work(struct work *work);

static bool clone_available(void)
{
  struct work *work_clone = NULL, *work, *tmp;
  bool cloned = false;

  mutex_lock(stgd_lock);
  if (!staged_rollable)
    goto out_unlock;

  HASH_ITER(hh, staged_work, work, tmp) {
    if (can_roll(work) && should_roll(work)) {
      roll_work(work);
      work_clone = make_clone(work);
      roll_work(work);
      cloned = true;
      break;
    }
  }

out_unlock:
  mutex_unlock(stgd_lock);

  if (cloned) {
    applog(LOG_DEBUG, "Pushing cloned available work to stage thread");
    stage_work(work_clone);
  }
  return cloned;
}

/* Clones work by rolling it if possible, and returning a clone instead of the
 * original work item which gets staged again to possibly be rolled again in
 * the future */
static struct work *clone_work(struct work *work)
{
  int mrs = mining_threads + opt_queue - total_staged();
  struct work *work_clone;
  bool cloned;

  if (mrs < 1)
    return work;

  cloned = false;
  work_clone = make_clone(work);
  while (mrs-- > 0 && can_roll(work) && should_roll(work)) {
    applog(LOG_DEBUG, "Pushing rolled converted work to stage thread");
    stage_work(work_clone);
    roll_work(work);
    work_clone = make_clone(work);
    /* Roll it again to prevent duplicates should this be used
     * directly later on */
    roll_work(work);
    cloned = true;
  }

  if (cloned) {
    stage_work(work);
    return work_clone;
  }

  free_work(work_clone);

  return work;
}

#else /* HAVE_LIBCURL */
static void *submit_work_thread(void __maybe_unused *userdata)
{
  pthread_detach(pthread_self());
  return NULL;
}
#endif /* HAVE_LIBCURL */

/* Return an adjusted ntime if we're submitting work that a device has
 * internally offset the ntime. */
static char *offset_ntime(const char *ntime, int noffset)
{
  unsigned char bin[4];
  uint32_t h32, *be32 = (uint32_t *)bin;

  hex2bin(bin, ntime, 4);
  h32 = be32toh(*be32) + noffset;
  *be32 = htobe32(h32);

  return bin2hex(bin, 4);
}

/* Duplicates any dynamically allocated arrays within the work struct to
 * prevent a copied work struct from freeing ram belonging to another struct */
static void _copy_work(struct work *work, const struct work *base_work, int noffset)
{
  int id = work->id;

  clean_work(work);
  memcpy(work, base_work, sizeof(struct work));
  /* Keep the unique new id assigned during make_work to prevent copied
   * work from having the same id. */
  work->id = id;
  if (base_work->job_id)
    work->job_id = strdup(base_work->job_id);
  if (base_work->nonce1)
    work->nonce1 = strdup(base_work->nonce1);
  if (base_work->ntime) {
    /* If we are passed an noffset the binary work->data ntime and
     * the work->ntime hex string need to be adjusted. */
    if (noffset) {
      uint32_t work_ntime = _get_work_time(work);
      uint32_t ntime = be32toh(work_ntime);
      ntime += noffset;
      _set_work_time(work, htobe32(ntime));
      work->ntime = offset_ntime(base_work->ntime, noffset);
    } else
      work->ntime = strdup(base_work->ntime);
  } else if (noffset) {
    uint32_t work_ntime = _get_work_time(work);
    uint32_t ntime = be32toh(work_ntime);
    ntime += noffset;
    _set_work_time(work, htobe32(ntime));
  }
  if (base_work->coinbase)
    work->coinbase = strdup(base_work->coinbase);
}

/* Generates a copy of an existing work struct, creating fresh heap allocations
 * for all dynamically allocated arrays within the struct. noffset is used for
 * when a driver has internally rolled the ntime, noffset is a relative value.
 * The macro copy_work() calls this function with an noffset of 0. */
struct work *copy_work_noffset(struct work *base_work, int noffset)
{
  struct work *work = make_work();

  _copy_work(work, base_work, noffset);

  return work;
}

void pool_failed(struct pool *pool)
{
  if (!pool_tset(pool, &pool->idle)) {
    cgtime(&pool->tv_idle);
    if (pool == current_pool()) {
      switch_pools(NULL);
    }
  }
}

static void pool_died(struct pool *pool)
{
  if (!pool_tset(pool, &pool->idle)) {
    cgtime(&pool->tv_idle);
    if (pool == current_pool()) {
      applog(LOG_WARNING, "%s not responding!", get_pool_name(pool));
      switch_pools(NULL);
    } else {
      applog(LOG_INFO, "%s failed to return work", get_pool_name(pool));
    }
  }
}

static bool stale_work(struct work *work, bool share)
{
  struct timeval now;
  time_t work_expiry;
  struct pool *pool;
  int getwork_delay;

  if (work->work_block != work_block) {
    applog(LOG_DEBUG, "Work stale due to block mismatch");
    return true;
  }

  /* Technically the rolltime should be correct but some pools
   * advertise a broken expire= that is lower than a meaningful
   * scantime */
  if (work->rolltime > opt_scantime)
    work_expiry = work->rolltime;
  else
    work_expiry = opt_expiry;

  pool = work->pool;

  if (!share && pool->has_stratum) {
    bool same_job;

    if (!pool->stratum_active || !pool->stratum_notify) {
      applog(LOG_DEBUG, "Work stale due to stratum inactive");
      return true;
    }

    same_job = true;

    cg_rlock(&pool->data_lock);
    if (strcmp(work->job_id, pool->swork.job_id))
      same_job = false;
    cg_runlock(&pool->data_lock);

    if (!same_job) {
      applog(LOG_DEBUG, "Work stale due to stratum job_id mismatch");
      return true;
    }
  }

  /* Factor in the average getwork delay of this pool, rounding it up to
   * the nearest second */
  getwork_delay = pool->sgminer_pool_stats.getwork_wait_rolling * 5 + 1;
  work_expiry -= getwork_delay;
  if (unlikely(work_expiry < 5))
    work_expiry = 5;

  cgtime(&now);
  if ((now.tv_sec - work->tv_staged.tv_sec) >= work_expiry) {
    applog(LOG_DEBUG, "Work stale due to expiry");
    return true;
  }

  if (opt_fail_only && !share && pool != current_pool() && !work->mandatory &&
      pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
    applog(LOG_DEBUG, "Work stale due to fail only pool mismatch");
    return true;
  }

  return false;
}

static double share_diff(const struct work *work)
{
  bool new_best = false;
  double d64, s64;
  double ret;

  ret = le256todiff(work->hash, work->pool->algorithm.share_diff_multiplier);
  applog(LOG_DEBUG, "Found share with difficulty %.3f", ret);

  cg_wlock(&control_lock);
  if (unlikely(ret > best_diff)) {
    new_best = true;
    best_diff = ret;
    suffix_string_double(best_diff, best_share, sizeof(best_share), 0);
  }
  if (unlikely(ret > work->pool->best_diff))
    work->pool->best_diff = ret;
  cg_wunlock(&control_lock);

  if (unlikely(new_best))
    applog(LOG_INFO, "New best share: %s", best_share);

  return ret;
}

static bool cnx_needed(struct pool *pool);

/* Find the pool that currently has the highest priority */
static struct pool *priority_pool(int choice)
{
  struct pool *ret = NULL;
  int i;

  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];

    if (pool->prio == choice) {
      ret = pool;
      break;
    }
  }

  if (unlikely(!ret)) {
    applog(LOG_ERR, "WTF No pool %d found!", choice);
    return pools[choice];
  }
  return ret;
}

/* Specifies whether we can switch to this pool or not. */
static bool pool_unusable(struct pool *pool)
{
  if (pool->idle)
    return true;
  if (pool->state != POOL_ENABLED)
    return true;
  return false;
}

void __switch_pools(struct pool *selected, bool saveprio)
{
  struct pool *pool, *last_pool;
  int i, pool_no, next_pool;

  cg_wlock(&control_lock);
  last_pool = currentpool;
  pool_no = currentpool->pool_no;

  /* If a specific pool was selected, prioritise it over others */
  if (selected && saveprio)
  {
    if (selected->prio != 0)
    {
      for (i = 0; i < total_pools; i++)
      {
        pool = pools[i];
        if (pool->prio < selected->prio)
          pool->prio++;
      }
      selected->prio = 0;
    }
  }

  switch (pool_strategy)
  {
    /* All of these set to the master pool */
    case POOL_BALANCE:
    case POOL_FAILOVER:
    case POOL_LOADBALANCE:
      for (i = 0; i < total_pools; i++)
      {
        pool = priority_pool(i);
        if (pool_unusable(pool))
          continue;
        pool_no = pool->pool_no;
        break;
      }
      break;
    /* Both of these simply increment and cycle */
    case POOL_ROUNDROBIN:
    case POOL_ROTATE:
      if (selected && !selected->idle)
      {
        pool_no = selected->pool_no;
        break;
      }

      next_pool = pool_no;

      /* Select the next alive pool */
      for (i = 1; i < total_pools; i++)
      {
        next_pool++;

        if (next_pool >= total_pools)
          next_pool = 0;

        pool = pools[next_pool];

        if (pool_unusable(pool))
          continue;

        pool_no = next_pool;
        break;
      }
      break;
    default:
      break;
  }

  currentpool = pools[pool_no];
  pool = currentpool;
  on_backup_pool = pool->backup;
  cg_wunlock(&control_lock);

  /* Set the lagging flag to avoid pool not providing work fast enough
   * messages in failover only mode since  we have to get all fresh work
   * as in restart_threads */
  if (opt_fail_only) {
    pool_tset(pool, &pool->lagging);
  }

  if (pool != last_pool && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
    //if the gpus have been initialized or first pool during startup, it's ok to switch...
    if(gpu_initialized || startup) {
      applog(LOG_WARNING, "Switching to %s", get_pool_name(pool));
      if (pool_localgen(pool) || opt_fail_only) {
        clear_pool_work(last_pool);
      }
    }
  }

  //if startup, initialize gpus and start mining threads
  if(startup) {
    startup = false;  //remove startup flag so we don't enter this block again
    applog(LOG_NOTICE, "Startup GPU initialization... Using settings from pool %s.", get_pool_name(pool));

    //set initial pool number for restart_mining_threads to prevent mismatched GPU settings
    init_pool = pool->pool_no;

    //apply gpu settings based on first alive pool
    apply_initial_gpu_settings(pool);

    gpu_initialized = true; //gpus initialized
  }

  mutex_lock(&lp_lock);
  pthread_cond_broadcast(&lp_cond);
  mutex_unlock(&lp_lock);
}

void discard_work(struct work *work)
{
  if (!work->clone && !work->rolls && !work->mined) {
    if (work->pool) {
      work->pool->discarded_work++;
      work->pool->quota_used--;
      work->pool->works--;
    }
    total_discarded++;
    applog(LOG_DEBUG, "[THR%d] Discarded work", work->thr_id);
  } else
    applog(LOG_DEBUG, "[THR%d] Discarded cloned or rolled work", work->thr_id);
  free_work(work);
}

static void wake_gws(void)
{
  mutex_lock(stgd_lock);
  pthread_cond_signal(&gws_cond);
  mutex_unlock(stgd_lock);
}

static void discard_stale(void)
{
  struct work *work, *tmp;
  int stale = 0;

  mutex_lock(stgd_lock);
  HASH_ITER(hh, staged_work, work, tmp) {
    if (stale_work(work, false)) {
      HASH_DEL(staged_work, work);
      discard_work(work);
      stale++;
    }
  }
  pthread_cond_signal(&gws_cond);
  mutex_unlock(stgd_lock);

  if (stale)
    applog(LOG_DEBUG, "Discarded %d stales that didn't match current hash", stale);
}

static void *restart_thread(void __maybe_unused *arg)
{
  struct pool *cp = current_pool();
  struct cgpu_info *cgpu;
  int i;

  pthread_detach(pthread_self());

  /* Artificially set the lagging flag to avoid pool not providing work
   * fast enough  messages after every long poll */
  pool_tset(cp, &cp->lagging);

  /* Discard staged work that is now stale */
  discard_stale();

  if (mining_thr) {
    rd_lock(&mining_thr_lock);
    for (i = 0; i < mining_threads; i++) {
      cgpu = mining_thr[i]->cgpu;
      if (unlikely(!cgpu))
        continue;
      if (cgpu->deven != DEV_ENABLED)
        continue;
      mining_thr[i]->work_restart = true;
      cgpu->drv->flush_work(cgpu);
    }
    rd_unlock(&mining_thr_lock);
  }

  mutex_lock(&restart_lock);
  pthread_cond_broadcast(&restart_cond);
  mutex_unlock(&restart_lock);

  return NULL;
}

/* In order to prevent a deadlock via the various drv->flush_work
 * implementations we send the restart messages via a separate thread. */
static void restart_threads(void)
{
  pthread_t rthread;

  if (unlikely(pthread_create(&rthread, NULL, restart_thread, NULL)))
    quit(1, "Failed to create restart thread");
}

static void signal_work_update(void)
{
  int i;

  applog(LOG_INFO, "Work update message received");

  rd_lock(&mining_thr_lock);
  for (i = 0; i < mining_threads; i++)
    mining_thr[i]->work_update = true;
  rd_unlock(&mining_thr_lock);
}

static void set_curblock(char *hexstr, unsigned char *bedata)
{
  int ofs;

  cg_wlock(&ch_lock);
  cgtime(&block_timeval);
  strcpy(current_hash, hexstr);
  memcpy(current_block, bedata, 32);
  get_timestamp(blocktime, sizeof(blocktime), &block_timeval);
  cg_wunlock(&ch_lock);

  for (ofs = 0; ofs <= 56; ofs++) {
    if (memcmp(&current_hash[ofs], "0", 1))
      break;
  }
  strncpy(prev_block, &current_hash[ofs], 8);
  prev_block[8] = '\0';

  applog(LOG_INFO, "New block: %s... diff %s", current_hash, block_diff);
}

/* Search to see if this string is from a block that has been seen before */
static bool block_exists(char *hexstr)
{
  struct block *s;

  rd_lock(&blk_lock);
  HASH_FIND_STR(blocks, hexstr, s);
  rd_unlock(&blk_lock);

  if (s)
    return true;
  return false;
}

static int block_sort(struct block *blocka, struct block *blockb)
{
  return blocka->block_no - blockb->block_no;
}

/* Decode the current block difficulty which is in packed form */
static void set_blockdiff(const struct work *work)
{
  double ddiff = get_work_blockdiff(work);

  if (unlikely(current_diff != ddiff)) {
    suffix_string(ddiff, block_diff, sizeof(block_diff), 0);
    current_diff = ddiff;
    if (opt_morenotices)
      applog(LOG_NOTICE, "Network diff set to %s", block_diff);
  }
}

static bool test_work_current(struct work *work)
{
  struct pool *pool = work->pool;
  unsigned char bedata[32];
  char hexstr[68];
  bool ret = true;

  if (work->mandatory)
    return ret;

  swap256(bedata, work->data + 4);
  __bin2hex(hexstr, bedata, 32);

  /* Search to see if this block exists yet and if not, consider it a
   * new block and set the current block details to this one */
  if (!block_exists(hexstr)) {
    struct block *s = (struct block *)calloc(sizeof(struct block), 1);
    int deleted_block = 0;

    if (unlikely(!s))
      quit (1, "test_work_current OOM");
    strcpy(s->hash, hexstr);
    s->block_no = new_blocks++;

    wr_lock(&blk_lock);
    /* Only keep the last hour's worth of blocks in memory since
     * work from blocks before this is virtually impossible and we
     * want to prevent memory usage from continually rising */
    if (HASH_COUNT(blocks) > 6) {
      struct block *oldblock;

      HASH_SORT(blocks, block_sort);
      oldblock = blocks;
      deleted_block = oldblock->block_no;
      HASH_DEL(blocks, oldblock);
      free(oldblock);
    }
    HASH_ADD_STR(blocks, hash, s);
    set_blockdiff(work);
    wr_unlock(&blk_lock);

    if (deleted_block)
      applog(LOG_DEBUG, "Deleted block %d from database", deleted_block);
    set_curblock(hexstr, bedata);
    /* Copy the information to this pool's prev_block since it
     * knows the new block exists. */
    memcpy(pool->prev_block, bedata, 32);
    if (unlikely(new_blocks == 1)) {
      ret = false;
      goto out;
    }

    work->work_block = ++work_block;
 if (opt_morenotices)
  {
    if (work->longpoll) {
      if (work->stratum) {
        applog(LOG_NOTICE, "Stratum from %s detected new block", get_pool_name(pool));
      } else {
        applog(LOG_NOTICE, "%sLONGPOLL from %s detected new block",
               work->gbt ? "GBT " : "", get_pool_name(pool));
      }
    } else if (have_longpoll)
      applog(LOG_NOTICE, "New block detected on network before pool notification");
    else
      applog(LOG_NOTICE, "New block detected on network");
  }
    restart_threads();
  } else {
    if (memcmp(pool->prev_block, bedata, 32)) {
      /* Work doesn't match what this pool has stored as
       * prev_block. Let's see if the work is from an old
       * block or the pool is just learning about a new
       * block. */
      if (memcmp(bedata, current_block, 32)) {
        /* Doesn't match current block. It's stale */
        applog(LOG_DEBUG, "Stale data from %s", get_pool_name(pool));
        ret = false;
      } else {
        /* Work is from new block and pool is up now
         * current. */
        applog(LOG_INFO, "%s now up to date", get_pool_name(pool));
        memcpy(pool->prev_block, bedata, 32);
      }
    }
#if 0
    /* This isn't ideal, this pool is still on an old block but
     * accepting shares from it. To maintain fair work distribution
     * we work on it anyway. */
    if (memcmp(bedata, current_block, 32))
      applog(LOG_DEBUG, "%s still on old block", get_pool_name(pool));
#endif
    if (work->longpoll) {
      work->work_block = ++work_block;
      if (shared_strategy() || work->pool == current_pool()) {
        if(opt_morenotices) {
          if (work->stratum)
            applog(LOG_NOTICE, "Stratum from %s requested work restart", get_pool_name(pool));
          else
            applog(LOG_NOTICE, "%sLONGPOLL from %s requested work restart", work->gbt ? "GBT " : "", get_pool_name(pool));
        }
        restart_threads();
      }
    }
  }
out:
  work->longpoll = false;

  return ret;
}

static int tv_sort(struct work *worka, struct work *workb)
{
  return worka->tv_staged.tv_sec - workb->tv_staged.tv_sec;
}

static bool work_rollable(struct work *work)
{
  return (!work->clone && work->rolltime);
}

static bool hash_push(struct work *work)
{
  bool rc = true;

  mutex_lock(stgd_lock);
  if (work_rollable(work))
    staged_rollable++;
  if (likely(!getq->frozen)) {
    HASH_ADD_INT(staged_work, id, work);
    HASH_SORT(staged_work, tv_sort);
  } else
    rc = false;
  pthread_cond_broadcast(&getq->cond);
  mutex_unlock(stgd_lock);

  return rc;
}

static void stage_work(struct work *work)
{
  applog(LOG_DEBUG, "[THR%d] Pushing work from %s to hash queue", work->thr_id, get_pool_name(work->pool));
  work->work_block = work_block;
  test_work_current(work);
  work->pool->works++;
  hash_push(work);
}

#ifdef HAVE_CURSES
int curses_int(const char *query)
{
  int ret;
  char *cvar;

  cvar = curses_input(query);
  ret = atoi(cvar);
  free(cvar);
  return ret;
}
#endif

#ifdef HAVE_CURSES
static bool input_pool(bool live);
#endif

#ifdef HAVE_CURSES
static void display_pool_summary(struct pool *pool)
{
  double efficiency = 0.0;

  if (curses_active_locked()) {
    wlog("Pool: %s\n", pool->rpc_url);
    if (pool->solved)
      wlog("SOLVED %d BLOCK%s!\n", pool->solved, pool->solved > 1 ? "S" : "");
    if (!pool->has_stratum)
      wlog("%s own long-poll support\n", pool->hdr_path ? "Has" : "Does not have");
    wlog(" Queued work requests: %d\n", pool->getwork_requested);
    wlog(" Share submissions: %d\n", pool->accepted + pool->rejected);
    wlog(" Accepted shares: %d\n", pool->accepted);
    wlog(" Rejected shares: %d\n", pool->rejected);
    wlog(" Accepted difficulty shares: %1.f\n", pool->diff_accepted);
    wlog(" Rejected difficulty shares: %1.f\n", pool->diff_rejected);
    if (pool->accepted || pool->rejected)
      wlog(" Reject ratio: %.1f%%\n", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));
    efficiency = pool->getwork_requested ? pool->accepted * 100.0 / pool->getwork_requested : 0.0;
    if (!pool_localgen(pool))
      wlog(" Efficiency (accepted / queued): %.0f%%\n", efficiency);

    wlog(" Items worked on: %d\n", pool->works);
    wlog(" Discarded work due to new blocks: %d\n", pool->discarded_work);
    wlog(" Stale submissions discarded due to new blocks: %d\n", pool->stale_shares);
    wlog(" Unable to get work from server occasions: %d\n", pool->getfail_occasions);
    wlog(" Submitting work remotely delay occasions: %d\n\n", pool->remotefail_occasions);
    unlock_curses();
  }
}
#endif

void zero_bestshare(void)
{
  int i;

  best_diff = 0;
  memset(best_share, 0, 8);
  suffix_string_double(best_diff, best_share, sizeof(best_share), 0);

  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];
    pool->best_diff = 0;
  }
}

void zero_stats(void)
{
  int i;

  cgtime(&total_tv_start);
  total_rolling = 0;
  total_mhashes_done = 0;
  total_getworks = 0;
  total_accepted = 0;
  total_rejected = 0;
  hw_errors = 0;
  total_stale = 0;
  total_discarded = 0;
  local_work = 0;
  total_go = 0;
  total_ro = 0;
  total_secs = 1.0;
  total_diff1 = 0;
  found_blocks = 0;
  total_diff_accepted = 0;
  total_diff_rejected = 0;
  total_diff_stale = 0;

  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];

    pool->getwork_requested = 0;
    pool->accepted = 0;
    pool->rejected = 0;
    pool->stale_shares = 0;
    pool->discarded_work = 0;
    pool->getfail_occasions = 0;
    pool->remotefail_occasions = 0;
    pool->last_share_time = 0;
    pool->diff1 = 0;
    pool->diff_accepted = 0;
    pool->diff_rejected = 0;
    pool->diff_stale = 0;
    pool->last_share_diff = 0;
  }

  zero_bestshare();

  for (i = 0; i < total_devices; ++i) {
    struct cgpu_info *cgpu = get_devices(i);

    mutex_lock(&hash_lock);
    cgpu->total_mhashes = 0;
    cgpu->accepted = 0;
    cgpu->rejected = 0;
    cgpu->hw_errors = 0;
    cgpu->utility = 0.0;
    cgpu->last_share_pool_time = 0;
    cgpu->diff1 = 0;
    cgpu->diff_accepted = 0;
    cgpu->diff_rejected = 0;
    cgpu->last_share_diff = 0;
    mutex_unlock(&hash_lock);

    /* Don't take any locks in the driver zero stats function, as
     * it's called async from everything else and we don't want to
     * deadlock. */
    cgpu->drv->zero_stats(cgpu);
  }
}

static void set_highprio(void)
{
#ifndef WIN32
  int ret = nice(-10);

  if (!ret)
    applog(LOG_DEBUG, "Unable to set thread to high priority");
#else
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

static void set_lowprio(void)
{
#ifndef WIN32
  int ret = nice(10);

  if (!ret)
    applog(LOG_INFO, "Unable to set thread to low priority");
#else
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
}

#ifdef HAVE_CURSES
static void display_pools(void)
{
  struct pool *pool;
  int selected, i;
  char input;

  opt_loginput = true;
  immedok(logwin, true);
  clear_logwin();
updated:
  for (i = 0; i < total_pools; i++) {
    pool = pools[i];

    if (pool == current_pool())
      wattron(logwin, A_BOLD);
    if (pool->state != POOL_ENABLED)
      wattron(logwin, A_DIM);
    wlogprint("%d: ", pool->pool_no);

    switch (pool->state) {
    case POOL_ENABLED:
      wlogprint("Enabled   ");
      break;
    case POOL_DISABLED:
      wlogprint("Disabled  ");
      break;
    case POOL_REJECTING:
      wlogprint("Rejecting ");
      break;
    case POOL_HIDDEN:
    default:
      break;
    }

    wlogprint("%s Quota %d  Prio %d '%s'  User:%s\n",
        pool->idle ? "Dead" : "Alive",
        pool->quota,
        pool->prio,
        get_pool_name(pool),
        get_pool_user(pool));
    wattroff(logwin, A_BOLD | A_DIM);
  }
retry:
  wlogprint("\nCurrent pool management strategy: %s\n",
    strategies[pool_strategy].s);
  if (pool_strategy == POOL_ROTATE)
    wlogprint("Set to rotate every %d minutes\n", opt_rotate_period);
  wlogprint("[F]ailover only %s\n", opt_fail_only ? "enabled" : "disabled");
  wlogprint("Pool [A]dd [R]emove [D]isable [E]nable [Q]uota change\n");
  wlogprint("[C]hange management strategy [S]witch pool [I]nformation\n");
  wlogprint("Or press any other key to continue\n");
  logwin_update();
  input = getch();

  if (!strncasecmp(&input, "a", 1)) {
    input_pool(true);
    goto updated;
  } else if (!strncasecmp(&input, "r", 1)) {
    if (total_pools <= 1) {
      wlogprint("Cannot remove last pool");
      goto retry;
    }
    selected = curses_int("Select pool number");
    if (selected < 0 || selected >= total_pools) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    pool = pools[selected];
    if (pool == current_pool())
      switch_pools(NULL);
    if (pool == current_pool()) {
      wlogprint("Unable to remove pool due to activity\n");
      goto retry;
    }
    disable_pool(pool);
    remove_pool(pool);
    goto updated;
  } else if (!strncasecmp(&input, "s", 1)) {
    selected = curses_int("Select pool number");
    if (selected < 0 || selected >= total_pools) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    pool = pools[selected];
    enable_pool(pool);
    switch_pools(pool);
    goto updated;
  } else if (!strncasecmp(&input, "d", 1)) {
    selected = curses_int("Select pool number");
    if (selected < 0 || selected >= total_pools) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    pool = pools[selected];
    disable_pool(pool);
    if (pool == current_pool())
      switch_pools(NULL);
    goto updated;
  } else if (!strncasecmp(&input, "e", 1)) {
    selected = curses_int("Select pool number");
    if (selected < 0 || selected >= total_pools) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    pool = pools[selected];
    enable_pool(pool);
    if (pool->prio < current_pool()->prio)
      switch_pools(pool);
    goto updated;
  } else if (!strncasecmp(&input, "c", 1)) {
    for (i = 0; i <= TOP_STRATEGY; i++)
      wlogprint("%d: %s\n", i, strategies[i].s);
    selected = curses_int("Select strategy number type");
    if (selected < 0 || selected > TOP_STRATEGY) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    if (selected == POOL_ROTATE) {
      opt_rotate_period = curses_int("Select interval in minutes");

      if (opt_rotate_period < 0 || opt_rotate_period > 9999) {
        opt_rotate_period = 0;
        wlogprint("Invalid selection\n");
        goto retry;
      }
    }
    pool_strategy = (enum pool_strategy)selected;
    switch_pools(NULL);
    goto updated;
  } else if (!strncasecmp(&input, "i", 1)) {
    selected = curses_int("Select pool number");
    if (selected < 0 || selected >= total_pools) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    pool = pools[selected];
    display_pool_summary(pool);
    goto retry;
  } else if (!strncasecmp(&input, "q", 1)) {
    selected = curses_int("Select pool number");
    if (selected < 0 || selected >= total_pools) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    pool = pools[selected];
    selected = curses_int("Set quota");
    if (selected < 0) {
      wlogprint("Invalid negative quota\n");
      goto retry;
    }
    pool->quota = selected;
    adjust_quota_gcd();
    goto updated;
  } else if (!strncasecmp(&input, "f", 1)) {
    opt_fail_only ^= true;
    goto updated;
  } else
    clear_logwin();

  immedok(logwin, false);
  opt_loginput = false;
}

static void display_options(void)
{
  int selected;
  char input;

  opt_loginput = true;
  immedok(logwin, true);

retry:
  clear_logwin();
  wlogprint("[N]ormal [C]lear [S]ilent mode (disable all output)\n");
  wlogprint("[D]ebug: %s\n[P]er-device: %s\n[Q]uiet: %s\n[V]erbose: %s\n"
      "[R]PC debug: %s\n[W]orkTime details: %s\n[I]ncognito: %s\n"
      "co[M]pact: %s\n[L]og interval: %d\n[Z]ero statistics\n",
    opt_debug_console ? "on" : "off",
          want_per_device_stats? "on" : "off",
    opt_quiet ? "on" : "off",
    opt_verbose ? "on" : "off",
    opt_protocol ? "on" : "off",
    opt_worktime ? "on" : "off",
    opt_incognito ? "on" : "off",
    opt_compact ? "on" : "off",
    opt_log_interval);
  wlogprint("Select an option or any other key to return\n");
  logwin_update();
  input = getch();
  if (!strncasecmp(&input, "q", 1)) {
    opt_quiet ^= true;
    wlogprint("Quiet mode %s\n", opt_quiet ? "enabled" : "disabled");
    goto retry;
  } else if (!strncasecmp(&input, "v", 1)) {
    opt_verbose ^= true;
    if (opt_verbose)
      opt_quiet = false;
    wlogprint("Verbose mode %s\n", opt_verbose ? "enabled" : "disabled");
    goto retry;
  } else if (!strncasecmp(&input, "n", 1)) {
    opt_verbose = false;
    opt_debug_console = false;
    opt_quiet = false;
    opt_protocol = false;
    opt_compact = false;
    want_per_device_stats = false;
    wlogprint("Output mode reset to normal\n");
    switch_logsize(false);
    goto retry;
  } else if (!strncasecmp(&input, "d", 1)) {
    opt_debug = true;
    opt_debug_console ^= true;
    opt_verbose = opt_debug_console;
    if (opt_debug_console)
      opt_quiet = false;
    wlogprint("Debug mode %s\n", opt_debug_console ? "enabled" : "disabled");
    goto retry;
  } else if (!strncasecmp(&input, "i", 1)) {
    opt_incognito ^= true;
    wlogprint("Incognito mode %s\n", opt_incognito ? "enabled" : "disabled");
    goto retry;
  } else if (!strncasecmp(&input, "m", 1)) {
    opt_compact ^= true;
    wlogprint("Compact mode %s\n", opt_compact ? "enabled" : "disabled");
    switch_logsize(false);
    goto retry;
  } else if (!strncasecmp(&input, "p", 1)) {
    want_per_device_stats ^= true;
    opt_verbose = want_per_device_stats;
    wlogprint("Per-device stats %s\n", want_per_device_stats ? "enabled" : "disabled");
    goto retry;
  } else if (!strncasecmp(&input, "r", 1)) {
    opt_protocol ^= true;
    if (opt_protocol)
      opt_quiet = false;
    wlogprint("RPC protocol debugging %s\n", opt_protocol ? "enabled" : "disabled");
    goto retry;
  } else if (!strncasecmp(&input, "c", 1))
    clear_logwin();
  else if (!strncasecmp(&input, "l", 1)) {
    selected = curses_int("Interval in seconds");
    if (selected < 0 || selected > 9999) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    opt_log_interval = selected;
    wlogprint("Log interval set to %d seconds\n", opt_log_interval);
    goto retry;
  } else if (!strncasecmp(&input, "s", 1)) {
    opt_realquiet = true;
  } else if (!strncasecmp(&input, "w", 1)) {
    opt_worktime ^= true;
    wlogprint("WorkTime details %s\n", opt_worktime ? "enabled" : "disabled");
    goto retry;
  } else if (!strncasecmp(&input, "z", 1)) {
    zero_stats();
    goto retry;
  } else
    clear_logwin();

  immedok(logwin, false);
  opt_loginput = false;
}
#endif /* HAVE_CURSES */

void default_save_file(char *filename)
{
  if (default_config && *default_config) {
    strcpy(filename, default_config);
    return;
  }

#if defined(unix) || defined(__APPLE__)
  if (getenv("HOME") && *getenv("HOME")) {
          strcpy(filename, getenv("HOME"));
    strcat(filename, "/");
  }
  else
    strcpy(filename, "");
  strcat(filename, ".sgminer/");
  mkdir(filename, 0777);
#else
  strcpy(filename, "");
#endif
  strcat(filename, def_conf);
}

#ifdef HAVE_CURSES
static void set_options(void)
{
  int selected;
  char input;

  opt_loginput = true;
  immedok(logwin, true);
  clear_logwin();
retry:
  wlogprint("[Q]ueue: %d\n[S]cantime: %d\n[E]xpiry: %d\n"
      "[W]rite config file\n[R]estart\n",
    opt_queue, opt_scantime, opt_expiry);
  wlogprint("Select an option or any other key to return\n");
  logwin_update();
  input = getch();

  if (!strncasecmp(&input, "q", 1)) {
    selected = curses_int("Extra work items to queue");
    if (selected < 0 || selected > 9999) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    opt_queue = selected;
    goto retry;
  } else if  (!strncasecmp(&input, "s", 1)) {
    selected = curses_int("Set scantime in seconds");
    if (selected < 0 || selected > 9999) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    opt_scantime = selected;
    goto retry;
  } else if  (!strncasecmp(&input, "e", 1)) {
    selected = curses_int("Set expiry time in seconds");
    if (selected < 0 || selected > 9999) {
      wlogprint("Invalid selection\n");
      goto retry;
    }
    opt_expiry = selected;
    goto retry;
  } else if  (!strncasecmp(&input, "w", 1)) {
    char *str, filename[PATH_MAX], prompt[PATH_MAX + 50];

    default_save_file(filename);
    snprintf(prompt, sizeof(prompt), "Config filename to write (Enter for default) [%s]", filename);
    str = curses_input(prompt);
    if (strcmp(str, "-1")) {
      struct stat statbuf;

      strcpy(filename, str);
      free(str);
      if (!stat(filename, &statbuf)) {
        wlogprint("File exists, overwrite?\n");
        input = getch();
        if (strncasecmp(&input, "y", 1))
          goto retry;
      }
    }
    else
      free(str);

    write_config(filename);

    goto retry;

  } else if (!strncasecmp(&input, "r", 1)) {
    wlogprint("Are you sure?\n");
    input = getch();
    if (!strncasecmp(&input, "y", 1))
      app_restart();
    else
      clear_logwin();
  } else
    clear_logwin();

  immedok(logwin, false);
  opt_loginput = false;
}

static void *input_thread(void __maybe_unused *userdata)
{
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  RenameThread("Input");

  if (!curses_active)
    return NULL;

  while (1) {
    char input;

    input = getch();
    if (!strncasecmp(&input, "q", 1)) {
      kill_work();
      return NULL;
    } else if (!strncasecmp(&input, "d", 1))
      display_options();
    else if (!strncasecmp(&input, "p", 1))
      display_pools();
    else if (!strncasecmp(&input, "s", 1))
      set_options();
    else if (!strncasecmp(&input, "g", 1))
      manage_gpu();
    if (opt_realquiet) {
      disable_curses();
      break;
    }
  }

  return NULL;
}
#endif /* HAVE_CURSES */

static void *api_thread(void *userdata)
{
  struct thr_info *mythr = (struct thr_info *)userdata;

  pthread_detach(pthread_self());
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  RenameThread("API");

  set_lowprio();
  api(api_thr_id);

  PTH(mythr) = 0L;

  return NULL;
}

/* Sole work devices are serialised wrt calling get_work so they report in on
 * each pass through their scanhash function as well as in get_work whereas
 * queued work devices work asynchronously so get them to report in and out
 * only across get_work. */
static void thread_reportin(struct thr_info *thr)
{
  cgtime(&thr->last);
  thr->getwork = false;
  thr->cgpu->status = LIFE_WELL;
  thr->cgpu->device_last_well = time(NULL);
}

/* Tell the watchdog thread this thread is waiting on get work and should not
 * be restarted */
static void thread_reportout(struct thr_info *thr)
{
  cgtime(&thr->last);
  thr->getwork = true;
  thr->cgpu->status = LIFE_WELL;
  thr->cgpu->device_last_well = time(NULL);
}

static void hashmeter(int thr_id, struct timeval *diff,
          uint64_t hashes_done)
{
  struct timeval temp_tv_end, total_diff;
  double secs;
  double local_secs;
  static double local_mhashes_done = 0;
  double local_mhashes;
  bool showlog = false;
  char displayed_hashes[16], displayed_rolling[16];
  uint64_t dh64, dr64;
  struct thr_info *thr = NULL;

  local_mhashes = (double)hashes_done / 1000000.0;
  /* Update the last time this thread reported in */
  rd_lock(&mining_thr_lock);
  if (thr_id >= 0 && thr_id < mining_threads) {
    thr = mining_thr[thr_id];
    cgtime(&thr->last);
    thr->cgpu->device_last_well = time(NULL);
  }
  rd_unlock(&mining_thr_lock);

  secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);

  /* So we can call hashmeter from a non worker thread */
  if (thr) {
    struct cgpu_info *cgpu = thr->cgpu;
    double thread_rolling = 0.0;
    int i;

    applog(LOG_DEBUG, "[thread %d: %"PRIu64" hashes, %.1f khash/sec]",
      thr_id, hashes_done, hashes_done / 1000 / secs);

    /* Rolling average for each thread and each device */
    decay_time(&thr->rolling, local_mhashes / secs, secs);
    for (i = 0; i < cgpu->threads; i++)
      thread_rolling += cgpu->thr[i]->rolling;

    mutex_lock(&hash_lock);
    decay_time(&cgpu->rolling, thread_rolling, secs);
    cgpu->total_mhashes += local_mhashes;
    mutex_unlock(&hash_lock);

    // If needed, output detailed, per-device stats
    if (want_per_device_stats) {
      struct timeval now;
      struct timeval elapsed;

      cgtime(&now);
      timersub(&now, &thr->cgpu->last_message_tv, &elapsed);
      if (opt_log_interval <= elapsed.tv_sec) {
        struct cgpu_info *cgpu = thr->cgpu;
        char logline[255];

        cgpu->last_message_tv = now;

        get_statline(logline, sizeof(logline), cgpu);
        if (!curses_active) {
          printf("%s          \r", logline);
          fflush(stdout);
        } else
          applog(LOG_INFO, "%s", logline);
      }
    }
  }

  /* Totals are updated by all threads so can race without locking */
  mutex_lock(&hash_lock);
  cgtime(&temp_tv_end);
  timersub(&temp_tv_end, &total_tv_end, &total_diff);

  total_mhashes_done += local_mhashes;
  local_mhashes_done += local_mhashes;
  /* Only update with opt_log_interval */
  if (total_diff.tv_sec < opt_log_interval)
    goto out_unlock;
  showlog = true;
  cgtime(&total_tv_end);

  local_secs = (double)total_diff.tv_sec + ((double)total_diff.tv_usec / 1000000.0);
  decay_time(&total_rolling, local_mhashes_done / local_secs, local_secs);
  global_hashrate = ((unsigned long long)lround(total_rolling)) * 1000000;

  timersub(&total_tv_end, &total_tv_start, &total_diff);
  total_secs = (double)total_diff.tv_sec +
    ((double)total_diff.tv_usec / 1000000.0);

  dh64 = (double)total_mhashes_done / total_secs * 1000000ull;
  dr64 = (double)total_rolling * 1000000ull;
  suffix_string(dh64, displayed_hashes, sizeof(displayed_hashes), 4);
  suffix_string(dr64, displayed_rolling, sizeof(displayed_rolling), 4);

  snprintf(statusline, sizeof(statusline),
    "%s(%ds):%s (avg):%sh/s | A:%.0f  R:%.0f  HW:%d  WU:%.3f/m",
    want_per_device_stats ? "ALL " : "",
    opt_log_interval, displayed_rolling, displayed_hashes,
    total_diff_accepted, total_diff_rejected, hw_errors,
    total_diff1 / total_secs * 60);

  local_mhashes_done = 0;
out_unlock:
  mutex_unlock(&hash_lock);

  if (showlog) {
    if (!curses_active) {
      printf("%s          \r", statusline);
      fflush(stdout);
    } else
      applog(LOG_INFO, "%s", statusline);
  }
}

static void stratum_share_result(json_t *val, json_t *res_val, json_t *err_val,
         struct stratum_share *sshare)
{
  struct work *work = sshare->work;
  time_t now_t = time(NULL);
  char hashshow[64];
  int srdiff;

  srdiff = now_t - sshare->sshare_sent;
  if (opt_debug || srdiff > 0) {
    applog(LOG_INFO, "Pool %d stratum share result lag time %d seconds",
           work->pool->pool_no, srdiff);
  }
  show_hash(work, hashshow);
  share_result(val, res_val, err_val, work, hashshow, false, "");
}

/* Parses stratum json responses and tries to find the id that the request
 * matched to and treat it accordingly. */
static bool parse_stratum_response(struct pool *pool, char *s)
{
  json_t *val = NULL, *err_val, *res_val, *id_val;
  struct stratum_share *sshare;
  json_error_t err;
  bool ret = false;
  int id;

  val = JSON_LOADS(s, &err);
  if (!val) {
    applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
    goto out;
  }

  res_val = json_object_get(val, "result");
  err_val = json_object_get(val, "error");
  id_val = json_object_get(val, "id");

  if ((json_is_null(id_val) || !id_val) && pool->algorithm.type != ALGO_ETHASH) {
    char *ss;

    if (err_val)
      ss = json_dumps(err_val, JSON_INDENT(3));
    else
      ss = strdup("(unknown reason)");

    applog(LOG_INFO, "JSON-RPC non method decode failed: %s", ss);

    free(ss);

    goto out;
  }

  id = json_integer_value(id_val);

  mutex_lock(&sshare_lock);
  HASH_FIND_INT(stratum_shares, &id, sshare);
  if (sshare) {
    HASH_DEL(stratum_shares, sshare);
    pool->sshares--;
  }
  mutex_unlock(&sshare_lock);

  if (!sshare) {
    double pool_diff;

    /* Since the share is untracked, we can only guess at what the
     * work difficulty is based on the current pool diff. */
    cg_rlock(&pool->data_lock);
    pool_diff = pool->swork.diff;
    cg_runlock(&pool->data_lock);

    if (json_is_true(res_val)) {
      applog(LOG_NOTICE, "Accepted untracked stratum share from %s", get_pool_name(pool));

      /* We don't know what device this came from so we can't
       * attribute the work to the relevant cgpu */
      mutex_lock(&stats_lock);
      total_accepted++;
      pool->accepted++;
      total_diff_accepted += pool_diff;
      pool->diff_accepted += pool_diff;
      mutex_unlock(&stats_lock);
    } else {
      applog(LOG_NOTICE, "Rejected untracked stratum share from %s", get_pool_name(pool));

      mutex_lock(&stats_lock);
      total_rejected++;
      pool->rejected++;
      total_diff_rejected += pool_diff;
      pool->diff_rejected += pool_diff;
      mutex_unlock(&stats_lock);
    }
    goto out;
  }
  stratum_share_result(val, res_val, err_val, sshare);
  free_work(sshare->work);
  free(sshare);

  ret = true;
out:
  if (val)
    json_decref(val);

  return ret;
}

static bool parse_stratum_response_bos(struct pool *pool, json_t *val)
{
	json_t  *err_val, *res_val, *id_val;
	struct stratum_share *sshare;
	json_error_t err;
	bool ret = false;
	int id;


	if (!val) {
		applog(LOG_INFO, "JSON decode failed: ");
		goto out;
	}

	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");
	id_val = json_object_get(val, "id");

	if ((json_is_null(id_val) || !id_val) && (pool->algorithm.type != ALGO_ETHASH)) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC non method decode failed: %s", ss);

		free(ss);

		goto out;
	}

	id = json_integer_value(id_val);

	mutex_lock(&sshare_lock);
	HASH_FIND_INT(stratum_shares, &id, sshare);
	if (sshare) {
		HASH_DEL(stratum_shares, sshare);
		pool->sshares--;
	}
	mutex_unlock(&sshare_lock);

	if (!sshare) {
		double pool_diff;
		bool success = false;

		/* Since the share is untracked, we can only guess at what the
		* work difficulty is based on the current pool diff. */
		cg_rlock(&pool->data_lock);
		pool_diff = pool->swork.diff;
		cg_runlock(&pool->data_lock);

		{
			success = json_is_true(res_val);
		}

		if (success) {
			applog(LOG_NOTICE, "Accepted untracked stratum share from %s", get_pool_name(pool));

			/* We don't know what device this came from so we can't
			* attribute the work to the relevant cgpu */
			mutex_lock(&stats_lock);
			total_accepted++;
			pool->accepted++;
			total_diff_accepted += pool_diff;
			pool->diff_accepted += pool_diff;
			mutex_unlock(&stats_lock);
		}
		else {
			applog(LOG_NOTICE, "Rejected untracked stratum share from %s", get_pool_name(pool));

			mutex_lock(&stats_lock);
			total_rejected++;
			pool->rejected++;
			total_diff_rejected += pool_diff;
			pool->diff_rejected += pool_diff;
			mutex_unlock(&stats_lock);
		}
		goto out;
	}
	stratum_share_result(val, res_val, err_val, sshare);
	free_work(sshare->work);
	free(sshare);

	ret = true;
out:
//	if (val)
//		json_decref(val);

	return ret;
}

void clear_stratum_shares(struct pool *pool)
{
  struct stratum_share *sshare, *tmpshare;
  double diff_cleared = 0;
  int cleared = 0;

  mutex_lock(&sshare_lock);
  HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
    if (sshare->work->pool == pool) {
      HASH_DEL(stratum_shares, sshare);
      diff_cleared += sshare->work->work_difficulty;
      free_work(sshare->work);
      pool->sshares--;
      free(sshare);
      cleared++;
    }
  }
  mutex_unlock(&sshare_lock);

  if (cleared) {
    applog(LOG_WARNING, "Lost %d shares due to stratum disconnect on %s", cleared, get_pool_name(pool));
    pool->stale_shares += cleared;
    total_stale += cleared;
    pool->diff_stale += diff_cleared;
    total_diff_stale += diff_cleared;
  }
}

void clear_pool_work(struct pool *pool)
{
  struct work *work, *tmp;
  int cleared = 0;

  mutex_lock(stgd_lock);
  HASH_ITER(hh, staged_work, work, tmp) {
    if (work->pool == pool) {
      HASH_DEL(staged_work, work);
      free_work(work);
      cleared++;
    }
  }
  mutex_unlock(stgd_lock);

  if (cleared)
    applog(LOG_INFO, "Cleared %d work items due to stratum disconnect on pool %d", cleared, pool->pool_no);
}

static int cp_prio(void)
{
  int prio;

  cg_rlock(&control_lock);
  prio = currentpool->prio;
  cg_runlock(&control_lock);

  return prio;
}

/* We only need to maintain a secondary pool connection when we need the
 * capacity to get work from the backup pools while still on the primary */
static bool cnx_needed(struct pool *pool)
{
  struct pool *cp;

  if (pool->state != POOL_ENABLED)
    return false;

  /* Balance strategies need all pools online */
  if (pool_strategy == POOL_BALANCE)
    return true;
  if (pool_strategy == POOL_LOADBALANCE)
    return true;

  /* Idle stratum pool needs something to kick it alive again */
  if (pool->has_stratum && pool->idle)
    return true;

  /* Getwork pools without opt_fail_only need backup pools up to be able
   * to leak shares */
  cp = current_pool();
  if (cp == pool)
    return true;
  if (!pool_localgen(cp) && (!opt_fail_only || !cp->hdr_path))
    return true;
  /* If we're waiting for a response from shares submitted, keep the
   * connection open. */
  if (pool->sshares)
    return true;
  /* If the pool has only just come to life and is higher priority than
   * the current pool keep the connection open so we can fail back to
   * it. */
  if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
    return true;
  if (pool_unworkable(cp))
    return true;
  /* We've run out of work, bring anything back to life. */
  if (no_work)
    return true;
  return false;
}

static void wait_lpcurrent(struct pool *pool);
static void pool_resus(struct pool *pool);
static void gen_stratum_work(struct pool *pool, struct work *work);
static void gen_stratum_work_eth(struct pool *pool, struct work *work);

static void stratum_resumed(struct pool *pool)
{
  if (!pool->stratum_notify)
    return;
  if (pool_tclear(pool, &pool->idle)) {
    applog(LOG_INFO, "Stratum connection to %s resumed", get_pool_name(pool));
    pool_resus(pool);
  }
}

static bool supports_resume(struct pool *pool)
{
  bool ret;

  cg_rlock(&pool->data_lock);
  ret = (pool->sessionid != NULL);
  cg_runlock(&pool->data_lock);

  return ret;
}

/* One stratum receive thread per pool that has stratum waits on the socket
 * checking for new messages and for the integrity of the socket connection. We
 * reset the connection based on the integrity of the receive side only as the
 * send side will eventually expire data it fails to send. */
static void *stratum_rthread(void *userdata)
{
  struct pool *pool = (struct pool *)userdata;
  char threadname[16];

  pthread_detach(pthread_self());

  snprintf(threadname, sizeof(threadname), "%d/RStratum", pool->pool_no);
  RenameThread(threadname);

  while (42) {
    struct timeval timeout;
    int sel_ret;
    fd_set rd;
    char *s;

    if (unlikely(pool->removed))
      break;

    /* Check to see whether we need to maintain this connection
     * indefinitely or just bring it up when we switch to this
     * pool */
    if (!sock_full(pool) && !cnx_needed(pool)) {
      applog(LOG_INFO, "Suspending stratum on %s",
             get_pool_name(pool));
      suspend_stratum(pool);
      clear_stratum_shares(pool);
      clear_pool_work(pool);

      wait_lpcurrent(pool);
      if (!restart_stratum(pool)) {
        pool_died(pool);
        while (!restart_stratum(pool)) {
          pool_failed(pool);
          if (pool->removed)
            goto out;
          cgsleep_ms(30000);
        }
      }
    }

    FD_ZERO(&rd);
    FD_SET(pool->sock, &rd);
    timeout.tv_sec = 150;
    timeout.tv_usec = 0;

    /* The protocol specifies that notify messages should be sent
     * every minute so if we fail to receive any for 90 seconds we
     * assume the connection has been dropped and treat this pool
     * as dead */
    if (!sock_full(pool) && (sel_ret = select(pool->sock + 1, &rd, NULL, NULL, &timeout)) < 1) {
      applog(LOG_DEBUG, "Stratum select failed on %s with value %d", get_pool_name(pool), sel_ret);
      s = NULL;
    } else
      s = (pool->algorithm.type == ALGO_MTP)? recv_line_bos(pool) : recv_line(pool);
    if (!s) {
      applog(LOG_NOTICE, "Stratum connection to %s interrupted", get_pool_name(pool));
      pool->getfail_occasions++;
      total_go++;

      /* If the socket to our stratum pool disconnects, all
       * tracked submitted shares are lost and we will leak
       * the memory if we don't discard their records. */
      if (!supports_resume(pool) || opt_lowmem)
        clear_stratum_shares(pool);
      clear_pool_work(pool);
      if (pool == current_pool())
        restart_threads();

      if (restart_stratum(pool))
        continue;

      pool_died(pool);
      while (!restart_stratum(pool)) {
        pool_failed(pool);
        if (pool->removed)
          goto out;
        cgsleep_ms(30000);
      }
      stratum_resumed(pool);
      continue;
    }

    /* Check this pool hasn't died while being a backup pool and
     * has not had its idle flag cleared */
    stratum_resumed(pool);

    if (!parse_method(pool, s) && !parse_stratum_response(pool, s))
      applog(LOG_INFO, "Unknown stratum msg: %s", s);
    else if (pool->swork.clean) {
      struct work *work = make_work();

      /* Generate a single work item to update the current
       * block database */
      pool->swork.clean = false;
      if(pool->algorithm.type == ALGO_ETHASH) gen_stratum_work_eth(pool, work);
      else gen_stratum_work(pool, work);
      work->longpoll = true;
      /* Return value doesn't matter. We're just informing
       * that we may need to restart. */
      test_work_current(work);
      free_work(work);
    }
    free(s);
  }

out:
  return NULL;
}

/* Each pool has one stratum send thread for sending shares to avoid many
 * threads being created for submission since all sends need to be serialised
 * anyway. */
static void *stratum_sthread(void *userdata)
{
  struct pool *pool = (struct pool *)userdata;
  char threadname[16];

  pthread_detach(pthread_self());

  snprintf(threadname, sizeof(threadname), "%d/SStratum", pool->pool_no);
  RenameThread(threadname);

  pool->stratum_q = tq_new();
  if (!pool->stratum_q)
    quit(1, "Failed to create stratum_q in stratum_sthread");

  size_t s_size = 4096;
  char *s = (char*) malloc(s_size);
  while (42) {
    char noncehex[12], nonce2hex[33];
    struct stratum_share *sshare;
    uint32_t *hash32, nonce;
    unsigned char nonce2[16];
    uint64_t *nonce2_64;
    struct work *work;
    bool submitted = false;

    if (unlikely(pool->removed)) {
      break;
    }

    work = (struct work *)tq_pop(pool->stratum_q, NULL);
    if (unlikely(!work))
      quit(1, "Stratum q returned empty work");

    hash32 = (uint32_t*) work->hash;
    if (!(sshare = (struct stratum_share *)calloc(sizeof(struct stratum_share), 1))) {
      quit(1, "%s: calloc() failed on sshare.", __func__);
    }

    if(pool->algorithm.type == ALGO_ETHASH) {
      sshare->sshare_time = time(NULL);
      /* This work item is freed in parse_stratum_response */
      sshare->work = work;

      applog(LOG_DEBUG, "stratum_sthread() algorithm = %s", pool->algorithm.name);

      uint64_t tmp = htobe64(work->Nonce);
      char *ASCIIMixHash = bin2hex(work->mixhash, 32);
      char *ASCIIPoWHash = bin2hex(work->data, 32);
      char *ASCIINonce = bin2hex((uint8_t*) &tmp + 4 - work->nonce2_len, 4 + work->nonce2_len);

      mutex_lock(&sshare_lock);
      /* Give the stratum share a unique id */
      sshare->id = swork_id++;
      mutex_unlock(&sshare_lock);
      snprintf(s, s_size, "{\"id\": %d, \"method\": \"mining.submit\", \"params\": [\"%s\", \"%s\", \"0x%s\", \"0x%s\", \"0x%s\"]}", sshare->id, pool->rpc_user, work->job_id, ASCIINonce, ASCIIPoWHash, ASCIIMixHash);

      free(ASCIINonce);
      free(ASCIIMixHash);
      free(ASCIIPoWHash);
    } else {
      if (unlikely(work->nonce2_len > 8)) {
        applog(LOG_ERR, "%s asking for inappropriately long nonce2 length %d", get_pool_name(pool), (int)work->nonce2_len);
        applog(LOG_ERR, "Not attempting to submit shares");
        free_work(work);
        continue;
      }

      sshare->sshare_time = time(NULL);
      /* This work item is freed in parse_stratum_response */
      sshare->work = work;

      applog(LOG_DEBUG, "stratum_sthread() algorithm = %s", pool->algorithm.name);

      // Neoscrypt is little endian
      if (pool->algorithm.type == ALGO_NEOSCRYPT || pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
          pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI) {
        nonce = htobe32(*((uint32_t *)(work->data + 76)));
        //*((uint32_t *)nonce2) = htole32(work->nonce2);
      }
      else if (pool->algorithm.type == ALGO_DECRED) {
        nonce = *((uint32_t *)(work->data + 140));
      }
      else if (pool->algorithm.type == ALGO_LBRY) {
        nonce = *((uint32_t *)(work->data + 108));
      }
      else if (pool->algorithm.type == ALGO_SIA) {
        nonce = *((uint32_t *)(work->data + 32));
      }
      else if (pool->algorithm.type == ALGO_PASCAL) {
        nonce = htobe32(*((uint32_t *)(work->data + 196)));
      }
      else {
        nonce = *((uint32_t *)(work->data + 76));
      }
      __bin2hex(noncehex, (const unsigned char *)&nonce, 4);

      *((uint64_t *)nonce2) = htole64(work->nonce2);
      __bin2hex(nonce2hex, nonce2, work->nonce2_len);
      memset(s, 0, 1024);

      mutex_lock(&sshare_lock);
      /* Give the stratum share a unique id */
      sshare->id = swork_id++;
      mutex_unlock(&sshare_lock);


      if (pool->algorithm.type == ALGO_DECRED && opt_vote) {
        snprintf(s, s_size,
          "{\"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%04x\"], \"id\": %d, \"method\": \"mining.submit\"}",
          pool->rpc_user, work->job_id, nonce2hex, work->ntime, noncehex, (opt_vote << 1) | 1, sshare->id);
      } else {
        snprintf(s, s_size,
          "{\"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"], \"id\": %d, \"method\": \"mining.submit\"}",
          pool->rpc_user, work->job_id, nonce2hex, work->ntime, noncehex, sshare->id);
      }
    }

    // applog(LOG_INFO, "Submitting share %08lx to %s", (long unsigned int)htole32(hash32[6]), get_pool_name(pool));

    /* Try resubmitting for up to 2 minutes if we fail to submit
     * once and the stratum pool nonce1 still matches suggesting
     * we may be able to resume. */
    while (time(NULL) < sshare->sshare_time + 120) {
      bool sessionid_match;

      mutex_lock(&sshare_lock);
      if (likely(stratum_send(pool, s, strlen(s)))) {
        int ssdiff;

        if (pool_tclear(pool, &pool->submit_fail))
            applog(LOG_WARNING, "%s communication resumed, submitting work", get_pool_name(pool));

        sshare->sshare_sent = time(NULL);
        ssdiff = sshare->sshare_sent - sshare->sshare_time;
        if (opt_debug || ssdiff > 0) {
          applog(LOG_INFO, "Pool %d stratum share submission lag time %d seconds",
                 pool->pool_no, ssdiff);
        }

        HASH_ADD_INT(stratum_shares, id, sshare);
        pool->sshares++;
        mutex_unlock(&sshare_lock);

        applog(LOG_DEBUG, "Successfully submitted, adding to stratum_shares db");
        submitted = true;
        break;
      }
      else {
        mutex_unlock(&sshare_lock);
      }
      if (!pool_tset(pool, &pool->submit_fail) && cnx_needed(pool)) {
        applog(LOG_WARNING, "%s stratum share submission failure", get_pool_name(pool));
        total_ro++;
        pool->remotefail_occasions++;
      }

      if (opt_lowmem) {
        applog(LOG_DEBUG, "Lowmem option prevents resubmitting stratum share");
        break;
      }

      cg_rlock(&pool->data_lock);
      sessionid_match = (pool->nonce1 && !strcmp(work->nonce1, pool->nonce1));
      cg_runlock(&pool->data_lock);

      if (!sessionid_match) {
        applog(LOG_DEBUG, "No matching session id for resubmitting stratum share");
        break;
      }
      /* Retry every 5 seconds */
      sleep(5);
    }

    if (unlikely(!submitted)) {
      applog(LOG_DEBUG, "Failed to submit stratum share, discarding");
      free_work(work);
      free(sshare);
      pool->stale_shares++;
      total_stale++;
    }
  }

  /* Freeze the work queue but don't free up its memory in case there is
   * work still trying to be submitted to the removed pool. */
  tq_freeze(pool->stratum_q);
  if (s != NULL)
    free(s);

  return NULL;
}

static void *stratum_sthread_bos(void *userdata)
{
//	printf("*************sending  thread bos******************\n");

	struct pool *pool = (struct pool *)userdata;
	char threadname[16];

	pthread_detach(pthread_self());

	snprintf(threadname, sizeof(threadname), "%d/SStratum", pool->pool_no);
	RenameThread(threadname);

	pool->stratum_q = tq_new();
	if (!pool->stratum_q)
		quit(1, "Failed to create stratum_q in stratum_sthread");


	bos_t *serialized;
	json_t *MyObject;
	uint32_t SizeMerkleRoot = 16;
	uint32_t SizeReserved = 64;
	uint32_t SizeMtpHash = 32;
	uint32_t SizeBlockMTP = MTP_L * 2 * 128 * 8;
	uint32_t SizeProofMTP = MTP_L * 3 * 353;
	uint32_t ntime;
	while (42) {
		char noncehex[12], nonce2hex[20];
		struct stratum_share *sshare;
		uint32_t *hash32, nonce;
		unsigned char nonce2[8];
		uint64_t *nonce2_64;
		struct work *work;
		bool submitted = false;

		if (unlikely(pool->removed))
			break;

		work = (struct work *)tq_pop(pool->stratum_q, NULL);
		if (unlikely(!work))
			quit(1, "Stratum q returned empty work");

		hash32 = (uint32_t*)work->hash;
		if (!(sshare = (struct stratum_share *)calloc(sizeof(struct stratum_share), 1))) {
			quit(1, "%s: calloc() failed on sshare.", __func__);
		}
		
		
			if (unlikely(work->nonce2_len > 8)) {
				applog(LOG_ERR, "%s asking for inappropriately long nonce2 length %d", get_pool_name(pool), (int)work->nonce2_len);
				applog(LOG_ERR, "Not attempting to submit shares");
				free_work(work);
				continue;
			}

			// TODO: check for memory leaks
			sshare->sshare_time = time(NULL);
			/* This work item is freed in parse_stratum_response */
			sshare->work = work;

			applog(LOG_DEBUG, "stratum_sthread_bos() algorithm = %s", pool->algorithm.name);

			unsigned char TheMerkle[16];
			memcpy(TheMerkle, pool->mtp_cache.mtpPOW.MerkleRoot, 16);
			applog(LOG_DEBUG, "stratum_sthread_bos THE MERKLE ROOT %08x %08x %08x %08x", ((uint32_t*)TheMerkle)[0], ((uint32_t*)TheMerkle)[1]
				, ((uint32_t*)TheMerkle)[2], ((uint32_t*)TheMerkle)[3]);

				ntime = htole32(((uint32_t*)work->data)[17]);
				nonce = htole32(pool->mtp_cache.mtpPOW.TheNonce);
			//	nonce = htole32(((uint32_t*)work->data)[19]);
			*((uint64_t *)nonce2) = htole64(work->nonce2);
			applog(LOG_DEBUG, "stratum_sthread_bos THE NONCE %08x ", nonce);

			mutex_lock(&sshare_lock);
			/* Give the stratum share a unique id */
			sshare->id = swork_id++;
			mutex_unlock(&sshare_lock);

			unsigned char hexjob_id[4];
			hex2bin(hexjob_id, work->job_id, 8);

      MyObject = json_object();
			json_t *json_arr = json_array();
			json_object_set_new(MyObject, "id", json_integer(sshare->id));
			json_object_set_new(MyObject, "method", json_string("mining.submit"));

      json_object_set_new(MyObject, "params", json_arr);

			json_array_append_new(json_arr, json_string(pool->rpc_user));
			json_array_append(json_arr, json_bytes((unsigned char*)hexjob_id, 4));
			json_array_append(json_arr, json_bytes((unsigned char*)&work->nonce2, sizeof(uint64_t*)));
			json_array_append(json_arr, json_bytes((unsigned char*)&ntime, sizeof(uint32_t)));
			json_array_append(json_arr, json_bytes((unsigned char*)&nonce, sizeof(uint32_t)));
			json_array_append(json_arr, json_bytes(pool->mtp_cache.mtpPOW.MerkleRoot, SizeMerkleRoot));
			json_array_append(json_arr, json_bytes((unsigned char*)pool->mtp_cache.mtpPOW.nBlockMTP, SizeBlockMTP));
			json_array_append(json_arr, json_bytes(pool->mtp_cache.mtpPOW.nProofMTP, SizeProofMTP));

			json_error_t boserror;
			bos_t *serialized = bos_serialize(MyObject, &boserror);



			applog(LOG_INFO, "Serialized size %d\n",serialized->size);
//			stratum.sharediff = work->sharediff[0];

		applog(LOG_INFO, "Submitting share %08lx to %s", (long unsigned int)htole32(hash32[6]), get_pool_name(pool));

		/* Try resubmitting for up to 2 minutes if we fail to submit
		* once and the stratum pool nonce1 still matches suggesting
		* we may be able to resume. */
		while (time(NULL) < sshare->sshare_time + 120) {
			bool sessionid_match;

			mutex_lock(&sshare_lock);
			if (likely(stratum_send_bos(pool, (char*)serialized->data, serialized->size))) {
				int ssdiff;

				if (pool_tclear(pool, &pool->submit_fail))
					applog(LOG_WARNING, "%s communication resumed, submitting work", get_pool_name(pool));

				sshare->sshare_sent = time(NULL);
				ssdiff = sshare->sshare_sent - sshare->sshare_time;
				if (opt_debug || ssdiff > 0) {
					applog(LOG_INFO, "Pool %d stratum share submission lag time %d seconds",
						pool->pool_no, ssdiff);
				}

				HASH_ADD_INT(stratum_shares, id, sshare);
				pool->sshares++;
				mutex_unlock(&sshare_lock);

				applog(LOG_DEBUG, "Successfully submitted, adding to stratum_shares db");
				submitted = true;
				break;
			}
			else {
				mutex_unlock(&sshare_lock);
			}
			if (!pool_tset(pool, &pool->submit_fail) && cnx_needed(pool)) {
				applog(LOG_WARNING, "%s stratum share submission failure", get_pool_name(pool));
				total_ro++;
				pool->remotefail_occasions++;
			}

			if (opt_lowmem) {
				applog(LOG_DEBUG, "Lowmem option prevents resubmitting stratum share");
				break;
			}

			cg_rlock(&pool->data_lock);
			sessionid_match = (pool->nonce1 && !strcmp(work->nonce1, pool->nonce1));
			cg_runlock(&pool->data_lock);

			if (!sessionid_match) {
				applog(LOG_DEBUG, "No matching session id for resubmitting stratum share");
				break;
			}
			/* Retry every 5 seconds */
			sleep(5);
		}

		if (unlikely(!submitted)) {
			applog(LOG_DEBUG, "Failed to submit stratum share, discarding");
			free_work(work);
			free(sshare);
			pool->stale_shares++;
			total_stale++;
		}
    if(MyObject!=NULL)
		  json_decref(MyObject);
	  if (serialized!=NULL)
		  bos_free(serialized);
	}

	/* Freeze the work queue but don't free up its memory in case there is
	* work still trying to be submitted to the removed pool. */
	tq_freeze(pool->stratum_q);
	
	return NULL;
}

static void init_stratum_threads(struct pool *pool)
{
  have_longpoll = true;

if (pool->algorithm.type == ALGO_MTP) {
	if (unlikely(pthread_create(&pool->stratum_sthread_bos, NULL, stratum_sthread_bos, (void *)pool)))
			quit(1, "Failed to create stratum sthread");
} else {
	if (unlikely(pthread_create(&pool->stratum_sthread, NULL, stratum_sthread, (void *)pool)))
			quit(1, "Failed to create stratum sthread");
}
  if (unlikely(pthread_create(&pool->stratum_rthread, NULL, stratum_rthread, (void *)pool)))
    quit(1, "Failed to create stratum rthread");
}

static void *longpoll_thread(void *userdata);

static bool stratum_works(struct pool *pool)
{
  applog(LOG_INFO, "Testing %s stratum %s", get_pool_name(pool), pool->stratum_url);
  if (!extract_sockaddr(pool->stratum_url, &pool->sockaddr_url, &pool->stratum_port))
    return false;

  if (pool->algorithm.type == ALGO_MTP) {
    if (!initiate_stratum_bos(pool))
    return false; 
  } else {
    if (!initiate_stratum(pool))
    return false;
  }

  return true;
}

static bool pool_active(struct pool *pool, bool pinging)
{
  struct timeval tv_getwork, tv_getwork_reply;
  bool ret = false;
  json_t *val, *ethval2;
  CURL *curl;
  char curl_err_str[CURL_ERROR_SIZE];
  int rolltime = 0;

  if (pool->has_gbt)
    applog(LOG_DEBUG, "Retrieving block template from %s", get_pool_name(pool));
  else
    applog(LOG_INFO, "Testing %s", get_pool_name(pool));

  /* This is the central point we activate stratum when we can */
retry_stratum:
  if (pool->has_stratum) {
    /* We create the stratum thread for each pool just after
     * successful authorisation. Once the init flag has been set
     * we never unset it and the stratum thread is responsible for
     * setting/unsetting the active flag */
    bool init = pool_tset(pool, &pool->stratum_init);

    if (!init) {
      bool ret = false;
      if (pool->algorithm.type == ALGO_MTP)
        ret = initiate_stratum_bos(pool) && auth_stratum_bos(pool);
      else 
	      ret = initiate_stratum(pool) && (!pool->extranonce_subscribe || subscribe_extranonce(pool)) && auth_stratum(pool);

      if (ret)
        init_stratum_threads(pool);
      else
        pool_tclear(pool, &pool->stratum_init);
      return ret;
    }
    return pool->stratum_active;
  }

  curl = curl_easy_init();
  if (unlikely(!curl)) {
    applog(LOG_ERR, "CURL initialisation failed");
    return false;
  }

  /* Probe for GBT support on first pass */
  if (!pool->probed) {
    applog(LOG_DEBUG, "Probing for GBT support");
    val = json_rpc_call(curl, curl_err_str, pool->rpc_url, pool->rpc_userpass,
            gbt_req, true, false, &rolltime, pool, false);
    if (val) {
      bool append = false, submit = false;
      json_t *res_val, *mutables;
      int i, mutsize = 0;

      res_val = json_object_get(val, "result");
      if (res_val) {
        mutables = json_object_get(res_val, "mutable");
        mutsize = json_array_size(mutables);
      }

      for (i = 0; i < mutsize; i++) {
        json_t *arrval = json_array_get(mutables, i);

        if (json_is_string(arrval)) {
          const char *mut = json_string_value(arrval);

          if (!strncasecmp(mut, "coinbase/append", 15))
            append = true;
          else if (!strncasecmp(mut, "submit/coinbase", 15))
            submit = true;
        }
      }
      json_decref(val);

      /* Only use GBT if it supports coinbase append and
       * submit coinbase */
      if (append && submit) {
        pool->has_gbt = true;
        pool->rpc_req = gbt_req;
      }
    }
    /* Reset this so we can probe fully just after this. It will be
     * set to true that time.*/
    pool->probed = false;

    if (pool->has_gbt)
      applog(LOG_DEBUG, "GBT coinbase + append support found, switching to GBT protocol");
    else
      applog(LOG_DEBUG, "No GBT coinbase + append support found, using getwork protocol");
  }

  cgtime(&tv_getwork);

  if(pool->algorithm.type == ALGO_ETHASH) {
    pool->rpc_req = eth_getwork_rpc;

    val = json_rpc_call(curl, curl_err_str, pool->rpc_url, pool->rpc_userpass,
          pool->rpc_req, true, false, &rolltime, pool, false);

    cgtime(&tv_getwork_reply);
  } else {
    val = json_rpc_call(curl, curl_err_str, pool->rpc_url, pool->rpc_userpass,
            pool->rpc_req, true, false, &rolltime, pool, false);
    cgtime(&tv_getwork_reply);

    /* Detect if a http getwork pool has an X-Stratum header at startup,
    * and if so, switch to that in preference to getwork if it works */
    if (pool->stratum_url && !opt_fix_protocol && stratum_works(pool)) {
      applog(LOG_NOTICE, "Switching %s to %s", get_pool_name(pool), pool->stratum_url);
      if (!pool->rpc_url)
        pool->rpc_url = strdup(pool->stratum_url);
      pool->has_stratum = true;
      curl_easy_cleanup(curl);

      goto retry_stratum;
    }
  }

  /* json_rpc_call() above succeeded */
  if (val) {
    struct work *work = make_work();
    bool rc;

    if(pool->algorithm.type == ALGO_ETHASH)
      rc = work_decode_eth(pool, work, val);
    else
      rc = work_decode(pool, work, val);

    if (rc) {
      applog(LOG_DEBUG, "Successfully retrieved and deciphered work from %s", get_pool_name(pool));
      work->pool = pool;
      work->rolltime = (pool->algorithm.type == ALGO_ETHASH) ? 0 : rolltime;
      copy_time(&work->tv_getwork, &tv_getwork);
      copy_time(&work->tv_getwork_reply, &tv_getwork_reply);
      work->getwork_mode = GETWORK_MODE_TESTPOOL;
      calc_diff(work, 0);
      applog(LOG_DEBUG, "Pushing pooltest work to base pool");

      stage_work(work);
      total_getworks++;
      pool->getwork_requested++;
      ret = true;
      cgtime(&pool->tv_idle);
    } else {
      applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher work from %s", get_pool_name(pool));
      free_work(work);
    }
    json_decref(val);

    if (pool->lp_url)
      goto out;

    /* Decipher the longpoll URL, if any, and store it in ->lp_url */
    if (pool->hdr_path) {
      char *copy_start, *hdr_path;
      bool need_slash = false;
      size_t siz;

      hdr_path = pool->hdr_path;
      if (strstr(hdr_path, "://")) {
        pool->lp_url = hdr_path;
        hdr_path = NULL;
      } else {
        /* absolute path, on current server */
        copy_start = (*hdr_path == '/') ? (hdr_path + 1) : hdr_path;
        if (pool->rpc_url[strlen(pool->rpc_url) - 1] != '/')
          need_slash = true;

        siz = strlen(pool->rpc_url) + strlen(copy_start) + 2;
        pool->lp_url = (char *)malloc(siz);
        if (!pool->lp_url) {
          applog(LOG_ERR, "Malloc failure in pool_active");
          goto out;
        }

        snprintf(pool->lp_url, siz, "%s%s%s", pool->rpc_url, need_slash ? "/" : "", copy_start);
      }
    } else
      pool->lp_url = NULL;

    if (!pool->lp_started) {
      pool->lp_started = true;
      if (unlikely(pthread_create(&pool->longpoll_thread, NULL, longpoll_thread, (void *)pool)))
        quit(1, "Failed to create pool longpoll thread");
    }
  } else {
    /* If we failed to parse a getwork, this could be a stratum
     * url without the prefix stratum+tcp:// so let's check it */
    if (initiate_stratum(pool)) {
      pool->has_stratum = true;
      goto retry_stratum;
    }
    applog(LOG_DEBUG, "FAILED to retrieve work from %s", get_pool_name(pool));
    if (!pinging)
      applog(LOG_WARNING, "%s slow/down or URL or credentials invalid", get_pool_name(pool));
  }
out:
  curl_easy_cleanup(curl);
  return ret;
}

static void pool_resus(struct pool *pool)
{
  if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
    applog(LOG_WARNING, "%s alive, testing stability", get_pool_name(pool));
  else
    applog(LOG_INFO, "%s alive", get_pool_name(pool));
}

/* If this is called non_blocking, it will return NULL for work so that must
 * be handled. */
static struct work *hash_pop(bool blocking)
{
  struct work *work = NULL, *tmp;
  int hc;

  mutex_lock(stgd_lock);
  if (!HASH_COUNT(staged_work)) {
    if (!blocking)
      goto out_unlock;
    do {
      struct timespec then;
      struct timeval now;
      int rc;

      cgtime(&now);
      then.tv_sec = now.tv_sec + 10;
      then.tv_nsec = now.tv_usec * 1000;
      pthread_cond_signal(&gws_cond);
      rc = pthread_cond_timedwait(&getq->cond, stgd_lock, &then);
      /* Check again for !no_work as multiple threads may be
        * waiting on this condition and another may set the
        * bool separately. */
      if (rc && !no_work) {
        no_work = true;
        applog(LOG_WARNING, "Waiting for work to be available from pools.");
        event_notify("idle");
      }
    } while (!HASH_COUNT(staged_work));
  }

  if (no_work) {
    applog(LOG_WARNING, "Work available from pools, resuming.");
    no_work = false;
  }

  hc = HASH_COUNT(staged_work);
  /* Find clone work if possible, to allow masters to be reused */
  if (hc > staged_rollable) {
    HASH_ITER(hh, staged_work, work, tmp) {
      if (!work_rollable(work))
        break;
    }
  } else
    work = staged_work;
  HASH_DEL(staged_work, work);
  if (work_rollable(work))
    staged_rollable--;

  /* Signal the getwork scheduler to look for more work */
  pthread_cond_signal(&gws_cond);

  /* Signal hash_pop again in case there are mutliple hash_pop waiters */
  pthread_cond_signal(&getq->cond);

  /* Keep track of last getwork grabbed */
  last_getwork = time(NULL);
out_unlock:
  mutex_unlock(stgd_lock);

  return work;
}

void set_target(unsigned char *dest_target, double diff, double diff_multiplier2, const int thr_id)
{
  unsigned char target[32];
  uint64_t *data64, h64;
  double d64, dcut64;

  if (unlikely(diff == 0.0)) {
    /* This shouldn't happen but best we check to prevent a crash */
    applog(LOG_ERR, "[THR%d] Diff zero passed to set_target", thr_id);
    diff = 1.0;
  }

  d64 = diff_multiplier2 * truediffone;
  d64 /= diff;

  dcut64 = d64 / bits192;
  h64 = dcut64;
  data64 = (uint64_t *)(target + 24);
  *data64 = htole64(h64);
  dcut64 = h64;
  dcut64 *= bits192;
  d64 -= dcut64;

  dcut64 = d64 / bits128;
  h64 = dcut64;
  data64 = (uint64_t *)(target + 16);
  *data64 = htole64(h64);
  dcut64 = h64;
  dcut64 *= bits128;
  d64 -= dcut64;

  dcut64 = d64 / bits64;
  h64 = dcut64;
  data64 = (uint64_t *)(target + 8);
  *data64 = htole64(h64);
  dcut64 = h64;
  dcut64 *= bits64;
  d64 -= dcut64;

  h64 = d64;
  data64 = (uint64_t *)(target);
  *data64 = htole64(h64);

  if (opt_debug) {
    char *htarget = bin2hex(target, 32);

    applog(LOG_DEBUG, "[THR%d] Generated target %s", thr_id, htarget);
    free(htarget);
  }
  memcpy(dest_target, target, 32);
}

/*****************************************************
 * Special set_target() function for Neoscrypt
 ****************************************************/
void set_target_neoscrypt(unsigned char *target, double diff, const int thr_id)
{
  uint64_t m;
  int k;

  diff /= 65536.0;
  for (k = 6; k > 0 && diff > 1.0; --k) {
    diff /= 4294967296.0;
  }

  m = 4294901760.0 / diff;

  if (m == 0 && k == 6) {
    memset(target, 0xff, 32);
  }
  else {
    memset(target, 0, 32);
    ((uint32_t *)target)[k] = (uint32_t)m;
    ((uint32_t *)target)[k + 1] = (uint32_t)(m >> 32);
  }

  if (opt_debug) {
    /* The target is computed in this systems endianess and stored
    * in its endianess on a uint32-level. But because the target are
    * eight uint32s, they are stored in mixed mode, i.e., each uint32
    * is stored in the local endianess, but the least significant bit
    * is stored in target[0] bit 0.
    *
    * To print this large number in a native human readable form the
    * order of the array entries is swapped, i.e., target[7] <-> target[0]
    * and each array entry is byte swapped to have the least significant
    * bit to the right. */
    uint32_t swaped[8];
    swab256(swaped, target);
    char *htarget = bin2hex((unsigned char *)swaped, 32);

    applog(LOG_DEBUG, "[THR%d] Generated neoscrypt target 0x%s", thr_id, htarget);
    free(htarget);
  }
}

/* Generates stratum based work based on the most recent notify information
 * from the pool. This will keep generating work while a pool is down so we use
 * other means to detect when the pool has died in stratum_thread */

static void gen_stratum_work_eth(struct pool *pool, struct work *work)
{
  if(pool->algorithm.type != ALGO_ETHASH)
    return;

  applog(LOG_DEBUG, "[THR%d] gen_stratum_work() - algorithm = %s", work->thr_id, pool->algorithm.name);

  cg_ilock(&pool->data_lock);
  uint32_t nonce2be = htobe32(pool->nonce2);
  if (pool->n1_len == 0) {
    cg_dlock(&pool->data_lock);
    mutex_lock(&eth_nonce_lock);
    nonce2be = htobe32(eth_nonce++);
    mutex_unlock(&eth_nonce_lock);
  }
  else {
    work->nonce1 = strdup(pool->nonce1);
    cg_ulock(&pool->data_lock);
    work->nonce2 = pool->nonce2++;
    cg_dwlock(&pool->data_lock);
  }
  memcpy(&nonce2be, pool->nonce1bin, pool->n1_len);
  work->Nonce = (uint64_t) be32toh(nonce2be) << 32;
  work->nonce2_len = pool->n2size;
  work->eth_epoch = pool->eth_cache.current_epoch;
  work->job_id = strdup(pool->swork.job_id);
  memcpy(work->data, pool->EthWork, 32);
  memcpy(work->target, pool->Target, 32);
  work->sdiff = pool->swork.diff;
  work->work_difficulty = pool->swork.diff;
  work->network_diff = pool->diff1;
  work->blk.nonce = 0;
  cg_runlock(&pool->data_lock);

  local_work++;
  work->pool = pool;
  work->stratum = true;
  work->blk.nonce = 0;
  work->id = total_work++;
  work->longpoll = false;
  work->getwork_mode = GETWORK_MODE_STRATUM;
  work->work_block = work->data[0];
  // Do not allow ntime rolling
  work->drv_rolllimit = 0;

  cgtime(&work->tv_staged);
}

/* Generates stratum based work based on the most recent notify information
 * from the pool. This will keep generating work while a pool is down so we use
 * other means to detect when the pool has died in stratum_thread */
static void gen_stratum_work(struct pool *pool, struct work *work)
{
  unsigned char merkle_root[32], merkle_sha[64];
  uint32_t *data32, *swap32;
  uint64_t nonce2le;
  int i, j;

  cg_wlock(&pool->data_lock);

  if (pool->algorithm.type == ALGO_PASCAL) {
/* TODO: refactor this */
    for (i = 0; i < 56; i += 8) {
        if (((pool->nonce2 >>  i) & 0xff) < 0x2d) pool->nonce2 = (pool->nonce2 & (0xffffffffffffff00 << i)) + (0x002d2d2d2d2d2d2d >> (48 - i));
        if (((pool->nonce2 >>  i) & 0xff) > 0xfe) pool->nonce2 = (pool->nonce2 & (0xffffffffffffff00 << i)) + (0x012d2d2d2d2d2d2d >> (48 - i));
    }
    if (((pool->nonce2 >> 56) & 0xff) < 0x2d) pool->nonce2 = 0x2d2d2d2d2d2d2d2d;
    if (((pool->nonce2 >> 56) & 0xff) > 0xfe) pool->nonce2 = 0x2d2d2d2d2d2d2d2d;
  }
  nonce2le = htole64(pool->nonce2);
  if (pool->algorithm.type != ALGO_DECRED && pool->algorithm.type != ALGO_SIA) {
    /* Update coinbase. Always use an LE encoded nonce2 to fill in values
    * from left to right and prevent overflow errors with small n2sizes */
    memcpy(pool->coinbase + pool->nonce2_offset, &nonce2le, pool->n2size);
  }
  if (pool->algorithm.type != ALGO_MTP) {
	  work->nonce2 = pool->nonce2++;
	  work->nonce2_len = pool->n2size;
  }

  /* Downgrade to a read lock to read off the pool variables */
  cg_dwlock(&pool->data_lock);

  if (pool->algorithm.type != ALGO_DECRED && pool->algorithm.type != ALGO_SIA && pool->algorithm.type != ALGO_PASCAL) {
    /* Generate merkle root */
    pool->algorithm.gen_hash(pool->coinbase, pool->swork.cb_len, merkle_root);
    memcpy(merkle_sha, merkle_root, 32);
    for (i = 0; i < pool->swork.merkles; i++) {
      memcpy(merkle_sha + 32, pool->swork.merkle_bin[i], 32);
      gen_hash(merkle_sha, 64, merkle_root);
      memcpy(merkle_sha, merkle_root, 32);
    }
  }

  applog(LOG_DEBUG, "[THR%d] gen_stratum_work() - algorithm = %s", work->thr_id, pool->algorithm.name);

  // Different for Neoscrypt because of Little Endian
  if (pool->algorithm.type == ALGO_NEOSCRYPT || pool->algorithm.type == ALGO_NEOSCRYPT_NAVI) {
    /* Incoming data is in little endian. */
    memcpy(merkle_root, merkle_sha, 32);

    uint32_t temp = pool->merkle_offset / sizeof(uint32_t), i;
    /* Put version (4 byte) + prev_hash (4 byte* 8) but big endian encoded
    * into work. */
    for (i = 0; i < temp; ++i) {
      ((uint32_t *)work->data)[i] = be32toh(((uint32_t *)pool->header_bin)[i]);
    }

    /* Now add the merkle_root (4 byte* 8), but it is encoded in little endian. */
    temp += 8;

    for (j = 0; i < temp; ++i, ++j) {
      ((uint32_t *)work->data)[i] = le32toh(((uint32_t *)merkle_root)[j]);
    }

    /* Add the time encoded in big endianess. */
    hex2bin((unsigned char *)&temp, pool->swork.ntime, 4);

    /* Add the nbits (big endianess). */
    ((uint32_t *)work->data)[17] = be32toh(temp);
    hex2bin((unsigned char *)&temp, pool->swork.nbit, 4);
    ((uint32_t *)work->data)[18] = be32toh(temp);
    ((uint32_t *)work->data)[20] = 0x80000000;
    ((uint32_t *)work->data)[31] = 0x00000280;
  } else if (pool->algorithm.type == ALGO_NEOSCRYPT_XAYA || pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI) {
    /* Incoming data is in little endian. */
    memcpy(merkle_root, merkle_sha, 32);

    uint32_t temp = pool->merkle_offset / sizeof(uint32_t), i;
    /* Put version (4 byte) + prev_hash (4 byte* 8) but big endian encoded
    * into work. */
    for (i = 0; i < temp; ++i) {
      ((uint32_t *)work->data)[i] = be32toh(((uint32_t *)pool->header_bin)[i]);
    }

    /* Now add the merkle_root (4 byte* 8), but it is encoded in little endian. */
    temp += 8;

    for (j = 0; i < temp; ++i, ++j) {
      ((uint32_t *)work->data)[i] = bswap_32(le32toh(((uint32_t *)merkle_root)[j]));
    }

    /* Add the time encoded in big endianess. */
    hex2bin((unsigned char *)&temp, pool->swork.ntime, 4);

    /* Add the nbits (big endianess). */
    ((uint32_t *)work->data)[17] = be32toh(temp);
    hex2bin((unsigned char *)&temp, pool->swork.nbit, 4);
    ((uint32_t *)work->data)[18] = be32toh(temp);
    ((uint32_t *)work->data)[20] = 0x80000000;
    ((uint32_t *)work->data)[31] = 0x00000280;
  } else  if (pool->algorithm.type == ALGO_MTP) {
	  /* Incoming data is in little endian. */
	  memcpy(merkle_root, merkle_sha, 32);
    
	  uint32_t temp;
	  memcpy(work->data, pool->header_bin, 128);
	  memcpy(work->data + pool->merkle_offset, merkle_root, 32);

	  /* Add the time encoded in little endianess. */
	  hex2bin((unsigned char *)&temp, pool->swork.ntime, 4);

	  /* Add the nbits (big endianess). */
	  ((uint32_t *)work->data)[17] = le32toh(temp);
	  hex2bin((unsigned char *)&temp, pool->swork.nbit, 4);
	  ((uint32_t *)work->data)[18] = le32toh(temp);
	  ((uint32_t *)work->data)[20] = 0x00100000;
	  ((uint32_t *)work->data)[31] = 0x00000000;
  }
  else if (pool->algorithm.type == ALGO_DECRED) {
    uint16_t vote = (uint16_t) (opt_vote << 1) | 1;
    size_t nonce2_offset = MIN(pool->n1_len, 36);
    memcpy(work->data, pool->header_bin, 4); // version
    flip32(work->data + 4, pool->header_bin + 4); // prevhash
    memcpy(work->data + 4 + 32, pool->coinbase, MIN((int)pool->swork.cb_len, 108));
    memcpy(work->data + 100, &vote, 2);
    for (i = 36; i < 45; i++)
      ((uint32_t *)work->data)[i] = 0;
    memcpy(work->data + 144, pool->nonce1bin, nonce2_offset);
    memcpy(work->data + 144 + nonce2_offset, &nonce2le, pool->n2size);
    size_t extranonce_len = MAX((int)pool->swork.cb_len - pool->nonce2_offset - pool->n2size, 0);
    memcpy(work->data + 180 - extranonce_len, pool->coinbase + pool->nonce2_offset + pool->n2size, extranonce_len);
  }
  else if (pool->algorithm.type == ALGO_SIA) {
    size_t nonce2_offset = MIN(pool->n1_len, 4);
    swab256(work->data, pool->header_bin + 4); // prevhash
    memcpy(work->data + 32 + 4, pool->nonce1bin, nonce2_offset);
    memcpy(work->data + 32 + 4 + nonce2_offset, &nonce2le, pool->n2size);
    memcpy(work->data + 32 + 8, pool->header_bin + 68, 4); // timestamp
    flip32(work->data + 32 + 8 + 8, pool->coinbase); // merkleroot
  }
  else if (pool->algorithm.type == ALGO_PASCAL) {
    uint32_t temp;
    memcpy(work->data, pool->coinbase, pool->swork.cb_len);
    hex2bin((unsigned char *)&temp, pool->swork.ntime, 4);
    /* Add the nbits (big endianess). */
    ((uint32_t *)work->data)[48] = be32toh(temp);
    ((uint32_t *)work->data)[49] = 0;
  }
  else if (pool->algorithm.type == ALGO_PHI2 || pool->algorithm.type == ALGO_PHI2_NAVI) {
    data32 = (uint32_t *)merkle_sha;
    swap32 = (uint32_t *)merkle_root;
    flip32(swap32, data32);

    /* Copy the data template from header_bin */
    memcpy(work->data, pool->header_bin, 144);
    memcpy(work->data + pool->merkle_offset, merkle_root, 32);
  }
  else if (pool->algorithm.type == ALGO_LYRA2ZZ) {
    data32 = (uint32_t *)merkle_sha;
    swap32 = (uint32_t *)merkle_root;
    flip32(swap32, data32);

    /* Copy the data template from header_bin */
    memcpy(work->data, pool->header_bin, 128);
    memcpy(work->data + pool->merkle_offset, merkle_root, 32);
  }
  else {
    data32 = (uint32_t *)merkle_sha;
    swap32 = (uint32_t *)merkle_root;
    flip32(swap32, data32);

    /* Copy the data template from header_bin */
    memcpy(work->data, pool->header_bin, 128);
    memcpy(work->data + pool->merkle_offset, merkle_root, 32);
  }

  /* Store the stratum work diff to check it still matches the pool's
  * stratum diff when submitting shares */
  work->sdiff = pool->swork.diff;

  /* Copy parameters required for share submission */
  work->job_id = strdup(pool->swork.job_id);
  work->nonce1 = strdup(pool->nonce1);
  work->ntime = strdup(pool->swork.ntime);
  cg_runlock(&pool->data_lock);

  if (opt_debug) {
    char *header, *merkle_hash;
    int datasize = 128;
    if (pool->algorithm.type == ALGO_DECRED) datasize = 180;
    else if (pool->algorithm.type == ALGO_PASCAL) datasize = 256;

    header = bin2hex(work->data, datasize);
    if (pool->algorithm.type != ALGO_DECRED && pool->algorithm.type != ALGO_SIA && pool->algorithm.type != ALGO_PASCAL) {
        merkle_hash = bin2hex((const unsigned char *)merkle_root, 32);
        applog(LOG_DEBUG, "[THR%d] Generated stratum merkle %s", work->thr_id, merkle_hash);
        free(merkle_hash);
    }
    applog(LOG_DEBUG, "[THR%d] Generated stratum header %s", work->thr_id, header);
    applog(LOG_DEBUG, "[THR%d] Work job_id %s nonce2 %"PRIu64" ntime %s", work->thr_id, work->job_id,
           work->nonce2, work->ntime);
    free(header);
  }

  // For Neoscrypt use set_target_neoscrypt() function
  if (pool->algorithm.type == ALGO_NEOSCRYPT || pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
      pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI ||
      pool->algorithm.type == ALGO_YESCRYPTR16 || pool->algorithm.type == ALGO_YESCRYPTR16_NAVI) {
    set_target_neoscrypt(work->target, work->sdiff, work->thr_id);
  } else if (pool->algorithm.type == ALGO_MTP){
	  memcpy(work->target, pool->Target, 32);
  } else {	
    if (pool->algorithm.calc_midstate) pool->algorithm.calc_midstate(work);
    set_target(work->target, work->sdiff, pool->algorithm.diff_multiplier2, work->thr_id);
  }

  local_work++;
  work->pool = pool;
  work->stratum = true;
  work->blk.nonce = 0;
  work->id = total_work++;
  work->longpoll = false;
  work->getwork_mode = GETWORK_MODE_STRATUM;
  work->work_block = work_block;
  /* Nominally allow a driver to ntime roll 60 seconds */
  work->drv_rolllimit = 60;
  calc_diff(work, work->sdiff);

  cgtime(&work->tv_staged);
}

static void enable_devices(void)
{
  int i;

  //enable/disable devices as needed
  if(opt_devs_enabled)
  {
    for (i = 0; i < total_devices; i++)
    {
      //device should be enabled
      if(devices_enabled[i])
        enable_device(i);
      else
      {
        //mark as disabled
        devices[i]->deven = DEV_DISABLED;
      }
    }
  }
  //enable all devices
  else
  {
    for (i = 0; i < total_devices; ++i)
      enable_device(i);
  }
}

static void apply_initial_gpu_settings(struct pool *pool)
{
  int i;
  const char *opt;
  unsigned long options;  //gpu adl options to apply
  unsigned int needed_threads = 0; //number of mining threads needed after we change devices

  applog(LOG_NOTICE, "Startup Pool No = %d", pool->pool_no);

  //get compare options
  options = compare_pool_settings(NULL, pool);

  //apply gpu settings
  rd_lock(&mining_thr_lock);

  apply_switcher_options(options, pool);

/*
  //reset devices
  opt_devs_enabled = 0;
  for (i = 0; i < MAX_DEVICES; i++)
      devices_enabled[i] = false;

  //assign pool devices if any
  if(!empty_string((opt = get_pool_setting(pool->devices, ((!empty_string(default_profile.devices))?default_profile.devices:"all"))))) {
    set_devices((char *)opt);
  }

  //lookup gap
  if(!empty_string((opt = get_pool_setting(pool->lookup_gap, default_profile.lookup_gap))))
    set_lookup_gap((char *)opt);

  //set intensity
  if(!empty_string((opt = get_pool_setting(pool->rawintensity, default_profile.rawintensity)))) {
      set_rawintensity((char *)opt);
  }
  else if(!empty_string((opt = get_pool_setting(pool->xintensity, default_profile.xintensity)))) {
    set_xintensity((char *)opt);
  }
  else if(!empty_string((opt = get_pool_setting(pool->intensity, ((!empty_string(default_profile.intensity))?default_profile.intensity:"8"))))) {
    set_intensity((char *)opt);
  }

  //shaders
  if(!empty_string((opt = get_pool_setting(pool->shaders, default_profile.shaders))))
    set_shaders((char *)opt);

  //thread-concurrency
  // neoscrypt - if not specified set TC to 0 so that TC will be calculated by intensity settings
  if (pool->algorithm.type == ALGO_NEOSCRYPT) {
    opt = ((empty_string(pool->thread_concurrency))?"0":get_pool_setting(pool->thread_concurrency, default_profile.thread_concurrency));
  }
  // otherwise use pool/profile setting or default to default profile setting
  else {
    opt = get_pool_setting(pool->thread_concurrency, default_profile.thread_concurrency);
  }

  if (!empty_string(opt)) {
    set_thread_concurrency(opt);
  }

  //worksize
  if(!empty_string((opt = get_pool_setting(pool->worksize, default_profile.worksize))))
    set_worksize(opt);
*/
  //manually apply algorithm
  for (i = 0; i < nDevs; i++)
  {
    applog(LOG_DEBUG, "Set GPU %d to %s", i, isnull(pool->algorithm.name, ""));
    gpus[i].algorithm = pool->algorithm;
  }
/*
  #ifdef HAVE_ADL
    options = APPLY_ENGINE | APPLY_MEMCLOCK | APPLY_FANSPEED | APPLY_POWERTUNE | APPLY_VDDC;

    //GPU clock
    if(!empty_string((opt = get_pool_setting(pool->gpu_engine, default_profile.gpu_engine))))
      set_gpu_engine((char *)opt);
    else
      options ^= APPLY_ENGINE;

    //GPU memory clock
    if(!empty_string((opt = get_pool_setting(pool->gpu_memclock, default_profile.gpu_memclock))))
      set_gpu_memclock((char *)opt);
    else
      options ^= APPLY_MEMCLOCK;

    //GPU fans
    if(!empty_string((opt = get_pool_setting(pool->gpu_fan, default_profile.gpu_fan))))
      set_gpu_fan((char *)opt);
    else
      options ^= APPLY_FANSPEED;

    //GPU powertune
    if(!empty_string((opt = get_pool_setting(pool->gpu_powertune, default_profile.gpu_powertune))))
      set_gpu_powertune((char *)opt);
    else
      options ^= APPLY_POWERTUNE;

    //GPU vddc
    if(!empty_string((opt = get_pool_setting(pool->gpu_vddc, default_profile.gpu_vddc))))
      set_gpu_vddc((char *)opt);
    else
      options ^= APPLY_VDDC;

    //apply gpu settings
    for (i = 0; i < nDevs; i++)
    {
      if(opt_isset(options, APPLY_ENGINE))
        set_engineclock(i, gpus[i].min_engine);
      if(opt_isset(options, APPLY_MEMCLOCK))
        set_memoryclock(i, gpus[i].gpu_memclock);
      if(opt_isset(options, APPLY_FANSPEED))
        set_fanspeed(i, gpus[i].min_fan);
      if(opt_isset(options, APPLY_POWERTUNE))
        set_powertune(i, gpus[i].gpu_powertune);
      if(opt_isset(options, APPLY_VDDC))
        set_vddc(i, gpus[i].gpu_vddc);
    }
  #endif*/

  rd_unlock(&mining_thr_lock);

  //enable/disable devices as needed
  enable_devices();

  //recount the number of needed mining threads
  #ifdef HAVE_ADL
    if(!empty_string((opt = get_pool_setting(pool->gpu_threads, default_profile.gpu_threads))))
      set_gpu_threads((char *)opt);

    rd_lock(&devices_lock);
    for (i = 0; i < total_devices; i++)
      if (!opt_removedisabled || !opt_devs_enabled || devices_enabled[i])
        needed_threads += devices[i]->threads;
    rd_unlock(&devices_lock);
  #else
    needed_threads = mining_threads;
  #endif

  //bad thread count?
  if(needed_threads == 0)
    quit(1, "No GPUs Initialized.");

  restart_mining_threads(needed_threads);
}

static unsigned long compare_pool_settings(struct pool *oldpool, struct pool *newpool)
{
  unsigned int options = 0;
  const char *opt1, *opt2;

  applog(LOG_DEBUG, "compare_pool_settings()");

  if (!newpool) {
    return 0;
  }

  //compare pool devices
  opt1 = ((oldpool)?get_pool_setting(oldpool->devices, ((!empty_string(default_profile.devices))?default_profile.devices:"all")):"");
  opt2 = get_pool_setting(newpool->devices, ((!empty_string(default_profile.devices))?default_profile.devices:"all"));

  //changing devices means a hard reset of mining threads
  if (strcasecmp(opt1, opt2) != 0) {
    options |= (SWITCHER_APPLY_DEVICE | SWITCHER_HARD_RESET);
  }

  //compare gpu threads
  opt1 = ((oldpool)?get_pool_setting(oldpool->gpu_threads, default_profile.gpu_threads):"");
  opt2 = get_pool_setting(newpool->gpu_threads, default_profile.gpu_threads);

  //changing gpu threads means a hard reset of mining threads
  if (strcasecmp(opt1, opt2) != 0) {
    options |= (SWITCHER_APPLY_GT | SWITCHER_HARD_RESET);
  }

  //compare algorithm
  if ((oldpool && !cmp_algorithm(&oldpool->algorithm, &newpool->algorithm)) || (!oldpool)) {
    options |= (SWITCHER_APPLY_ALGO | SWITCHER_SOFT_RESET);
  }

  //lookup gap
  opt1 = ((oldpool)?get_pool_setting(oldpool->lookup_gap, default_profile.lookup_gap):"");
  opt2 = get_pool_setting(newpool->lookup_gap, default_profile.lookup_gap);

  //lookup gap means soft reset but only if hard reset isnt set
  if (strcasecmp(opt1, opt2) != 0) {
    options |= (SWITCHER_APPLY_LG | SWITCHER_SOFT_RESET);
  }

  // Intensity - First determine the intensity type that we are going to use with the new pool
  unsigned int intoptions = 0;

  if (!empty_string(newpool->rawintensity)) {
    intoptions = SWITCHER_APPLY_RAWINT | SWITCHER_SOFT_RESET;
    opt1 = ((oldpool && !empty_string(oldpool->rawintensity))?oldpool->rawintensity:"");
    opt2 = get_pool_setting(newpool->rawintensity, default_profile.rawintensity);
  }
  else if (!empty_string(newpool->xintensity)) {
    intoptions = SWITCHER_APPLY_XINT | SWITCHER_SOFT_RESET;
    opt1 = ((oldpool && !empty_string(oldpool->xintensity))?oldpool->xintensity:"");
    opt2 = get_pool_setting(newpool->xintensity, default_profile.xintensity);
  }
  else {
    intoptions = SWITCHER_APPLY_INT | SWITCHER_SOFT_RESET;
    opt1 = ((oldpool && !empty_string(oldpool->intensity))?oldpool->intensity:"");
    opt2 = get_pool_setting(newpool->intensity, default_profile.intensity);
  }

  // if old intensity and new intensity different, set flags to update
  if(strcasecmp(opt1, opt2) != 0)  {
    // in case we compared 2 empty strings make sure new intensity is not empty
    if (!empty_string(opt2)) {
      options |= intoptions;
    }
  }

  //shaders
  opt1 = ((oldpool)?get_pool_setting(oldpool->shaders, default_profile.shaders):"");
  opt2 = get_pool_setting(newpool->shaders, default_profile.shaders);

  if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
    options |= (SWITCHER_APPLY_SHADER | SWITCHER_SOFT_RESET);
  }

  //thread-concurrency
  // neoscrypt - if not specified set TC to 0 so that TC will be calculated by intensity settings
  if (newpool->algorithm.type == ALGO_NEOSCRYPT || newpool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
      newpool->algorithm.type == ALGO_NEOSCRYPT_NAVI || newpool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI) {
    opt2 = ((empty_string(newpool->thread_concurrency))?"0":get_pool_setting(newpool->thread_concurrency, default_profile.thread_concurrency));
  }
  // otherwise use pool/profile setting or default to default profile setting
  else {
    opt2 = get_pool_setting(newpool->thread_concurrency, default_profile.thread_concurrency);
  }

  opt1 = ((oldpool)?get_pool_setting(oldpool->thread_concurrency, default_profile.thread_concurrency):"");

  //thread-concurrency is soft reset
  if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
    options |= (SWITCHER_APPLY_TC | SWITCHER_SOFT_RESET);
  }

  //worksize
  opt1 = ((oldpool)?get_pool_setting(oldpool->worksize, default_profile.worksize):"");
  opt2 = get_pool_setting(newpool->worksize, default_profile.worksize);

  //worksize is soft reset
  if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
      options |= (SWITCHER_APPLY_WORKSIZE | SWITCHER_SOFT_RESET);
  }

  #ifdef HAVE_ADL
    //gpu-engine
    opt1 = ((oldpool)?get_pool_setting(oldpool->gpu_engine, default_profile.gpu_engine):"");
    opt2 = get_pool_setting(newpool->gpu_engine, default_profile.gpu_engine);

    if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
      options |= SWITCHER_APPLY_GPU_ENGINE;
    }

    //gpu-memclock
    opt1 = ((oldpool)?get_pool_setting(oldpool->gpu_memclock, default_profile.gpu_memclock):"");
    opt2 = get_pool_setting(newpool->gpu_memclock, default_profile.gpu_memclock);

    if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
      options |= SWITCHER_APPLY_GPU_MEMCLOCK;
    }

    //GPU fans
    opt1 = ((oldpool)?get_pool_setting(oldpool->gpu_fan, default_profile.gpu_fan):"");
    opt2 = get_pool_setting(newpool->gpu_fan, default_profile.gpu_fan);

    if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
      options |= SWITCHER_APPLY_GPU_FAN;
    }

    //GPU powertune
    opt1 = ((oldpool)?get_pool_setting(oldpool->gpu_powertune, default_profile.gpu_powertune):"");
    opt2 = get_pool_setting(newpool->gpu_powertune, default_profile.gpu_powertune);

    if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
      options |= SWITCHER_APPLY_GPU_POWERTUNE;
    }

    //GPU vddc
    opt1 = ((oldpool)?get_pool_setting(oldpool->gpu_vddc, default_profile.gpu_vddc):"");
    opt2 = get_pool_setting(newpool->gpu_vddc, default_profile.gpu_vddc);

    if (strcasecmp(opt1, opt2) != 0 && !empty_string(opt2)) {
      options |= SWITCHER_APPLY_GPU_VDDC;
    }
  #endif

  // Remove soft reset if hard reset is set
  if (opt_isset(options, SWITCHER_HARD_RESET) && opt_isset(options, SWITCHER_SOFT_RESET)) {
    options &= ~SWITCHER_SOFT_RESET;
  }

  return options;
}

static void apply_switcher_options(unsigned long options, struct pool *pool)
{
  int i;
  const char *opt;

  //nothing to change, abort
  if (!options) {
    return;
  }

  if(opt_isset(options, SWITCHER_APPLY_DEVICE))
  {
    //reset devices flags
    opt_devs_enabled = 0;
    for (i = 0; i < MAX_DEVICES; i++)
        devices_enabled[i] = false;

    //assign pool devices if any
    if(!empty_string((opt = get_pool_setting(pool->devices, ((!empty_string(default_profile.devices))?default_profile.devices:"all"))))) {
      set_devices((char *)opt);
    }
  }

  //lookup gap
  if(opt_isset(options, SWITCHER_APPLY_LG))
  {
    if(!empty_string((opt = get_pool_setting(pool->lookup_gap, default_profile.lookup_gap))))
      set_lookup_gap((char *)opt);
  }

  //raw intensity from pool
  if(opt_isset(options, SWITCHER_APPLY_RAWINT))
  {
    applog(LOG_DEBUG, "Switching to rawintensity: pool = %s, default = %s", pool->rawintensity, default_profile.rawintensity);
    opt = get_pool_setting(pool->rawintensity, default_profile.rawintensity);
    applog(LOG_DEBUG, "rawintensity -> %s", opt);
    set_rawintensity(opt);
  }
  //xintensity
  else if(opt_isset(options, SWITCHER_APPLY_XINT))
  {
    applog(LOG_DEBUG, "Switching to xintensity: pool = %s, default = %s", pool->xintensity, default_profile.xintensity);
    opt = get_pool_setting(pool->xintensity, default_profile.xintensity);
    applog(LOG_DEBUG, "xintensity -> %s", opt);
    set_xintensity(opt);
  }
  //intensity
  else if(opt_isset(options, SWITCHER_APPLY_INT))
  {
    applog(LOG_DEBUG, "Switching to intensity: pool = %s, default = %s", pool->intensity, default_profile.intensity);
    opt = get_pool_setting(pool->intensity, default_profile.intensity);
    applog(LOG_DEBUG, "intensity -> %s", opt);
    set_intensity(opt);
  }
  //default basic intensity
  else if(opt_isset(options, SWITCHER_APPLY_INT8))
  {
    default_profile.intensity = strdup("8");
    set_intensity(default_profile.intensity);
  }

  //shaders
  if(opt_isset(options, SWITCHER_APPLY_SHADER))
  {
    if(!empty_string((opt = get_pool_setting(pool->shaders, default_profile.shaders))))
      set_shaders((char *)opt);
  }

  //thread-concurrency
  if(opt_isset(options, SWITCHER_APPLY_TC))
  {
    // neoscrypt - if not specified set TC to 0 so that TC will be calculated by intensity settings
    if (pool->algorithm.type == ALGO_NEOSCRYPT || pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
        pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI) {
      opt = ((empty_string(pool->thread_concurrency))?"0":get_pool_setting(pool->thread_concurrency, default_profile.thread_concurrency));
    }
    // otherwise use pool/profile setting or default to default profile setting
    else {
      opt = get_pool_setting(pool->thread_concurrency, default_profile.thread_concurrency);
    }

    if(!empty_string(opt)) {
      set_thread_concurrency((char *)opt);
    }
  }

  //worksize
  if(opt_isset(options, SWITCHER_APPLY_WORKSIZE))
  {
    if(!empty_string((opt = get_pool_setting(pool->worksize, default_profile.worksize))))
      set_worksize(opt);
  }

  #ifdef HAVE_ADL
    //GPU clock
    if(opt_isset(options, SWITCHER_APPLY_GPU_ENGINE))
    {
      if(!empty_string((opt = get_pool_setting(pool->gpu_engine, default_profile.gpu_engine))))
        set_gpu_engine((char *)opt);
    }

    //GPU memory clock
    if(opt_isset(options, SWITCHER_APPLY_GPU_MEMCLOCK))
    {
      if(!empty_string((opt = get_pool_setting(pool->gpu_memclock, default_profile.gpu_memclock))))
        set_gpu_memclock((char *)opt);
    }

    //GPU fans
    if(opt_isset(options, SWITCHER_APPLY_GPU_FAN))
    {
      if(!empty_string((opt = get_pool_setting(pool->gpu_fan, default_profile.gpu_fan))))
        set_gpu_fan((char *)opt);
    }

    //GPU powertune
    if(opt_isset(options, SWITCHER_APPLY_GPU_POWERTUNE))
    {
      if(!empty_string((opt = get_pool_setting(pool->gpu_powertune, default_profile.gpu_powertune))))
        set_gpu_powertune((char *)opt);
    }

    //GPU vddc
    if(opt_isset(options, SWITCHER_APPLY_GPU_VDDC))
    {
      if(!empty_string((opt = get_pool_setting(pool->gpu_vddc, default_profile.gpu_vddc))))
        set_gpu_vddc((char *)opt);
    }

    //apply gpu settings
    for (i = 0; i < nDevs; ++i) {
      if(opt_isset(options, SWITCHER_APPLY_GPU_ENGINE))
        set_engineclock(i, gpus[i].min_engine);
      if(opt_isset(options, SWITCHER_APPLY_GPU_MEMCLOCK))
        set_memoryclock(i, gpus[i].gpu_memclock);
      if(opt_isset(options, SWITCHER_APPLY_GPU_FAN))
        set_fanspeed(i, gpus[i].min_fan);
#ifdef HAVE_CURSES
      if(opt_isset(options, SWITCHER_APPLY_GPU_POWERTUNE))
        set_powertune(i, gpus[i].gpu_powertune);
#endif
      if(opt_isset(options, SWITCHER_APPLY_GPU_VDDC))
        set_vddc(i, gpus[i].gpu_vddc);
    }
  #endif
}

static void mutex_unlock_cleanup_handler(void *mutex)
{
  mutex_unlock((pthread_mutex_t *) mutex);
}

static void get_work_prepare_thread(struct thr_info *mythr, struct work *work)
{
  int i;

  applog(LOG_DEBUG, "[THR%d] get_work_prepare_thread", mythr->id);

  //if switcher is disabled
  if(opt_switchmode == SWITCH_OFF)
    return;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  mutex_lock(&algo_switch_lock);

  if(algo_switch_n == 0)
  {
    //get pool options to apply on switch
    pool_switch_options = 0;

    //switcher mode - switch on algorithm change
    if(opt_switchmode == SWITCH_ALGO)
    {
      //if algorithms are different
      if(!cmp_algorithm(&work->pool->algorithm, &mythr->cgpu->algorithm))
        pool_switch_options = compare_pool_settings(pools[mythr->pool_no], work->pool);
    }
    //switcher mode - switch on pool change
    else if(opt_switchmode == SWITCH_POOL)
    {
      if(work->pool->pool_no != mythr->pool_no)
      {
        if((pool_switch_options = compare_pool_settings(pools[mythr->pool_no], work->pool)) == 0)
        {
          applog(LOG_DEBUG, "No settings change from pool %s...", isnull(get_pool_name(work->pool), ""));

          rd_lock(&mining_thr_lock);

          //apply new pool_no to all mining threads
          for (i = 0; i < mining_threads; i++)
          {
            struct thr_info *thr = mining_thr[i];
            thr->pool_no = work->pool->pool_no;
          }

          rd_unlock(&mining_thr_lock);
        }
      }
    }

    if(pool_switch_options == 0)
    {
      mutex_unlock(&algo_switch_lock);
      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
      return;
    }
  }

  mutex_lock(&algo_switch_wait_lock);
  algo_switch_n++;
  mutex_unlock(&algo_switch_wait_lock);

  //get the number of active threads to know when to switch... if we only check total threads, we may wait for ever on a disabled GPU
  int active_threads = 0;

  rd_lock(&mining_thr_lock);
  for(i = 0; i < mining_threads; i++)
  {
    struct cgpu_info *cgpu = mining_thr[i]->cgpu;

    //dont count dead/sick GPU threads or we may wait for ever also...
    if(cgpu->deven != DEV_DISABLED && cgpu->status != LIFE_SICK && cgpu->status != LIFE_DEAD)
      active_threads++;
  }
  rd_unlock(&mining_thr_lock);

  // If all threads are waiting now
  if(algo_switch_n >= active_threads)
  {
    const char *opt;

    applog(LOG_DEBUG, "Applying pool settings for %s...", isnull(get_pool_name(work->pool), ""));
    rd_lock(&mining_thr_lock);

    // Shutdown all threads first (necessary)
    if(opt_isset(pool_switch_options, SWITCHER_SOFT_RESET))
    {
      applog(LOG_DEBUG, "Soft Reset... Shutdown threads...");
      for (i = 0; i < mining_threads; i++)
      {
        struct thr_info *thr = mining_thr[i];
        thr->cgpu->drv->thread_shutdown(thr);
      }
    }

    // Reset stats (e.g. for working_diff to be set properly in hash_sole_work)
    zero_stats();

    //apply switcher options
    apply_switcher_options(pool_switch_options, work->pool);

    //devices
    /*if(opt_isset(pool_switch_options, SWITCHER_APPLY_DEVICE))
    {
      //reset devices flags
      opt_devs_enabled = 0;
      for (i = 0; i < MAX_DEVICES; i++)
          devices_enabled[i] = false;

      //assign pool devices if any
      if(!empty_string((opt = get_pool_setting(work->pool->devices, ((!empty_string(default_profile.devices))?default_profile.devices:"all"))))) {
        set_devices((char *)opt);
      }
    }

    //lookup gap
    if(opt_isset(pool_switch_options, SWITCHER_APPLY_LG))
    {
      if(!empty_string((opt = get_pool_setting(work->pool->lookup_gap, default_profile.lookup_gap))))
        set_lookup_gap((char *)opt);
    }

    //raw intensity from pool
    if(opt_isset(pool_switch_options, SWITCHER_APPLY_RAWINT))
    {
      applog(LOG_DEBUG, "Switching to rawintensity: pool = %s, default = %s", work->pool->rawintensity, default_profile.rawintensity);
      opt = get_pool_setting(work->pool->rawintensity, default_profile.rawintensity);
      applog(LOG_DEBUG, "rawintensity -> %s", opt);
      set_rawintensity(opt);
    }
    //xintensity
    else if(opt_isset(pool_switch_options, SWITCHER_APPLY_XINT))
    {
      applog(LOG_DEBUG, "Switching to xintensity: pool = %s, default = %s", work->pool->xintensity, default_profile.xintensity);
      opt = get_pool_setting(work->pool->xintensity, default_profile.xintensity);
      applog(LOG_DEBUG, "xintensity -> %s", opt);
      set_xintensity(opt);
    }
    //intensity
    else if(opt_isset(pool_switch_options, SWITCHER_APPLY_INT))
    {
      applog(LOG_DEBUG, "Switching to intensity: pool = %s, default = %s", work->pool->intensity, default_profile.intensity);
      opt = get_pool_setting(work->pool->intensity, default_profile.intensity);
      applog(LOG_DEBUG, "intensity -> %s", opt);
      set_intensity(opt);
    }
    //default basic intensity
    else if(opt_isset(pool_switch_options, SWITCHER_APPLY_INT8))
    {
      default_profile.intensity = strdup("8");
      set_intensity(default_profile.intensity);
    }

    //shaders
    if(opt_isset(pool_switch_options, SWITCHER_APPLY_SHADER))
    {
      if(!empty_string((opt = get_pool_setting(work->pool->shaders, default_profile.shaders))))
        set_shaders((char *)opt);
    }

    //thread-concurrency
    if(opt_isset(pool_switch_options, SWITCHER_APPLY_TC))
    {
      // neoscrypt - if not specified set TC to 0 so that TC will be calculated by intensity settings
      if (work->pool->algorithm.type == ALGO_NEOSCRYPT) {
        opt = ((empty_string(work->pool->thread_concurrency))?"0":get_pool_setting(work->pool->thread_concurrency, default_profile.thread_concurrency));
      }
      // otherwise use pool/profile setting or default to default profile setting
      else {
        opt = get_pool_setting(work->pool->thread_concurrency, default_profile.thread_concurrency);
      }

      if(!empty_string(opt)) {
        set_thread_concurrency((char *)opt);
      }
    }

    //worksize
    if(opt_isset(pool_switch_options, SWITCHER_APPLY_WORKSIZE))
    {
      if(!empty_string((opt = get_pool_setting(work->pool->worksize, default_profile.worksize))))
        set_worksize(opt);
    }

    #ifdef HAVE_ADL
      //GPU clock
      if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_ENGINE))
      {
        if(!empty_string((opt = get_pool_setting(work->pool->gpu_engine, default_profile.gpu_engine))))
          set_gpu_engine((char *)opt);
      }

      //GPU memory clock
      if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_MEMCLOCK))
      {
        if(!empty_string((opt = get_pool_setting(work->pool->gpu_memclock, default_profile.gpu_memclock))))
          set_gpu_memclock((char *)opt);
      }

      //GPU fans
      if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_FAN))
      {
        if(!empty_string((opt = get_pool_setting(work->pool->gpu_fan, default_profile.gpu_fan))))
          set_gpu_fan((char *)opt);
      }

      //GPU powertune
      if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_POWERTUNE))
      {
        if(!empty_string((opt = get_pool_setting(work->pool->gpu_powertune, default_profile.gpu_powertune))))
          set_gpu_powertune((char *)opt);
      }

      //GPU vddc
      if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_VDDC))
      {
        if(!empty_string((opt = get_pool_setting(work->pool->gpu_vddc, default_profile.gpu_vddc))))
          set_gpu_vddc((char *)opt);
      }

      //apply gpu settings
      for (i = 0; i < nDevs; i++)
      {
        if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_ENGINE))
          set_engineclock(i, gpus[i].min_engine);
        if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_MEMCLOCK))
          set_memoryclock(i, gpus[i].gpu_memclock);
        if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_FAN))
          set_fanspeed(i, gpus[i].min_fan);
        if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_POWERTUNE))
          set_powertune(i, gpus[i].gpu_powertune);
        if(opt_isset(pool_switch_options, SWITCHER_APPLY_GPU_VDDC))
          set_vddc(i, gpus[i].gpu_vddc);
      }
    #endif
  */
    // Change algorithm for each thread (thread_prepare calls initCl)
    if(opt_isset(pool_switch_options, SWITCHER_SOFT_RESET))
      applog(LOG_DEBUG, "Soft Reset... Restarting threads...");

    struct thr_info *thr;

    for (i = 0; i < mining_threads; i++)
    {
      thr = mining_thr[i];
      thr->pool_no = work->pool->pool_no; //set thread on new pool

      //apply new algorithm if set
      if(opt_isset(pool_switch_options, SWITCHER_APPLY_ALGO))
        thr->cgpu->algorithm = work->pool->algorithm;

      if(opt_isset(pool_switch_options, SWITCHER_SOFT_RESET))
      {
        thr->cgpu->drv->thread_prepare(thr);
        thr->cgpu->drv->thread_init(thr);
      }

      // Necessary because algorithms can have dramatically different diffs
      thr->cgpu->drv->working_diff = 1;
    }

    rd_unlock(&mining_thr_lock);
    mutex_unlock(&algo_switch_lock);

    // Hard restart if needed
    if(opt_isset(pool_switch_options, SWITCHER_HARD_RESET))
    {
      applog(LOG_DEBUG, "Hard Reset Mining Threads...");

      //if devices changed... enable/disable as needed
      if(opt_isset(pool_switch_options, SWITCHER_APPLY_DEVICE))
        enable_devices();

      //figure out how many mining threads we'll need
      unsigned int n_threads = 0;
      pthread_t restart_thr;

      #ifdef HAVE_ADL
        //change gpu threads if needed
        if(opt_isset(pool_switch_options, SWITCHER_APPLY_GT))
        {
          if(!empty_string((opt = get_pool_setting(work->pool->gpu_threads, default_profile.gpu_threads))))
            set_gpu_threads(opt);
        }

        rd_lock(&devices_lock);
        for (i = 0; i < total_devices; i++)
          if (!opt_removedisabled || !opt_devs_enabled || devices_enabled[i])
            n_threads += devices[i]->threads;
        rd_unlock(&devices_lock);
      #else
        n_threads = mining_threads;
      #endif

      if (unlikely(pthread_create(&restart_thr, NULL, restart_mining_threads_thread, (void *) (intptr_t) n_threads)))
        quit(1, "restart_mining_threads create thread failed");

      applog(LOG_DEBUG, "Hard reset: Exiting mining thread %d", mythr->id);
      pthread_exit(NULL);
    }
    else
    {
      // Signal other threads to start working now
      mutex_lock(&algo_switch_wait_lock);
      algo_switch_n = 0;
      pthread_cond_broadcast(&algo_switch_wait_cond);
      mutex_unlock(&algo_switch_wait_lock);

      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

      // no need to wait, exit
      return;
    }
  }
  else {
    bool hard_reset = opt_isset(pool_switch_options, SWITCHER_HARD_RESET);
    mutex_unlock(&algo_switch_lock);

    if (hard_reset) {
      applog(LOG_DEBUG, "Hard reset: Exiting mining thread %d", mythr->id);
      pthread_exit(NULL);
    }
  }

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  // Set cleanup instructions in the event that the thread is cancelled
  pthread_cleanup_push(mutex_unlock_cleanup_handler, (void *)&algo_switch_wait_lock);
  // Wait for signal to start working again
  mutex_lock(&algo_switch_wait_lock);
  while(algo_switch_n > 0)
    pthread_cond_wait(&algo_switch_wait_cond, &algo_switch_wait_lock);
  // Non-zero argument will execute the cleanup handler after popping it
  pthread_cleanup_pop(1);
}

struct work *get_work(struct thr_info *thr, const int thr_id)
{
  struct work *work = NULL;
  time_t diff_t;

  thread_reportout(thr);
  applog(LOG_DEBUG, "[THR%d] Popping work from get queue to get work", thr_id);
  diff_t = time(NULL);
  while (!work) {
    work = hash_pop(true);
    if (stale_work(work, false)) {
      applog(LOG_DEBUG, "[THR%d] Work is stale, discarding", thr_id);
      discard_work(work);
      work = NULL;
      wake_gws();
    }
  }

  applog(LOG_DEBUG, "[THR%d] preparing thread...", thr_id);
  get_work_prepare_thread(thr, work);

  diff_t = time(NULL) - diff_t;
  /* Since this is a blocking function, we need to add grace time to
   * the device's last valid work to not make outages appear to be
   * device failures. */
  if (diff_t > 0) {
    applog(LOG_DEBUG, "[THR%d] Get work blocked for %d seconds", thr_id, (int)diff_t);
    thr->cgpu->last_device_valid_work += diff_t;
  }
  applog(LOG_DEBUG, "[THR%d] Got work from get queue", thr_id);

  work->thr_id = thr_id;
  work->thr = thr;
  thread_reportin(thr);
  work->mined = true;
  work->device_diff = MIN(thr->cgpu->drv->max_diff, work->work_difficulty);
  return work;
}

/* Submit a copy of the tested, statistic recorded work item asynchronously */
static void submit_work_async(struct work *work)
{
  struct pool *pool = work->pool;
  pthread_t submit_thread;

  cgtime(&work->tv_work_found);

  if (stale_work(work, true)) {
    if (opt_submit_stale)
      applog(LOG_NOTICE, "%s stale share detected, submitting (user)", get_pool_name(pool));
    else if (pool->submit_old)
      applog(LOG_NOTICE, "%s stale share detected, submitting (pool)", get_pool_name(pool));
    else {
      applog(LOG_NOTICE, "%s stale share detected, discarding", get_pool_name(pool));
      sharelog("discard", work);

      mutex_lock(&stats_lock);
      total_stale++;
      pool->stale_shares++;
      total_diff_stale += work->work_difficulty;
      pool->diff_stale += work->work_difficulty;
      mutex_unlock(&stats_lock);

      free_work(work);
      return;
    }
    work->stale = true;
  }

  if (work->stratum) {
    applog(LOG_DEBUG, "Pushing %s work to stratum queue", get_pool_name(pool));
    if (unlikely(!tq_push(pool->stratum_q, work))) {
      applog(LOG_DEBUG, "Discarding work from removed pool");
      free_work(work);
    }
  } else {
    applog(LOG_DEBUG, "Pushing submit work to work thread");
    if (unlikely(pthread_create(&submit_thread, NULL, submit_work_thread, (void *)work)))
      quit(1, "Failed to create submit_work_thread");
  }
}

void inc_hw_errors(struct thr_info *thr)
{
  applog(LOG_INFO, "[THR%d] %s%d: invalid nonce - HW error", thr->id, thr->cgpu->drv->name,
         thr->cgpu->device_id);

  mutex_lock(&stats_lock);
  hw_errors++;
  thr->cgpu->hw_errors++;
  mutex_unlock(&stats_lock);

  thr->cgpu->drv->hw_error(thr);
}

/* Fills in the work nonce and builds the output data in work->hash */
static void rebuild_nonce(struct work *work, uint32_t nonce)
{
  uint32_t nonce_pos = 76;
  if (work->pool->algorithm.type == ALGO_CRE) nonce_pos = 140;
  else if (work->pool->algorithm.type == ALGO_DECRED) nonce_pos = 140;
  else if (work->pool->algorithm.type == ALGO_LBRY) nonce_pos = 108;
  else if (work->pool->algorithm.type == ALGO_SIA) nonce_pos = 32;
  else if (work->pool->algorithm.type == ALGO_PASCAL) nonce_pos = 196;

  if (work->pool->algorithm.type == ALGO_ETHASH) {
    work->Nonce += nonce;
  } else {
    uint32_t *work_nonce = (uint32_t *)(work->data + nonce_pos);

    *work_nonce = htole32(nonce);
  }

  work->pool->algorithm.regenhash(work);
}

/* For testing a nonce against diff 1 */
bool test_nonce(struct work *work, uint32_t nonce)
{
  uint32_t *hash_32 = (uint32_t *)(work->hash + 28);
  uint32_t diff1targ;

  rebuild_nonce(work, nonce);

  // for Neoscrypt, the diff1targ value is in work->target
  if (work->pool->algorithm.type == ALGO_NEOSCRYPT || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
      work->pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI || work->pool->algorithm.type == ALGO_PLUCK
    || work->pool->algorithm.type == ALGO_YESCRYPT || work->pool->algorithm.type == ALGO_YESCRYPT_MULTI
    || work->pool->algorithm.type == ALGO_YESCRYPT_NAVI
    || work->pool->algorithm.type == ALGO_YESCRYPTR16
    || work->pool->algorithm.type == ALGO_YESCRYPTR16_NAVI
    || work->pool->algorithm.type == ALGO_ARGON2D) {
    diff1targ = ((uint32_t *)work->target)[7];
  }
  else if (work->pool->algorithm.type == ALGO_ETHASH) {
    return fulltest(work->hash, work->device_target);
  }
  else {
    diff1targ = work->pool->algorithm.diff1targ;
  }

  return (le32toh(*hash_32) <= diff1targ);
}

static void update_work_stats(struct thr_info *thr, struct work *work)
{
  double test_diff = current_diff;

  work->share_diff = share_diff(work);

  test_diff *= work->pool->algorithm.share_diff_multiplier;

  if (unlikely(test_diff > 0 && work->share_diff >= test_diff)) {
    work->block = true;
    work->pool->solved++;
    found_blocks++;
    work->mandatory = true;
    applog(LOG_NOTICE, "Found block for %s!", get_pool_name(work->pool));
  }

  mutex_lock(&stats_lock);
  total_diff1 += work->device_diff;
  thr->cgpu->diff1 += work->device_diff;
  work->pool->diff1 += work->device_diff;
  thr->cgpu->last_device_valid_work = time(NULL);
  mutex_unlock(&stats_lock);
}

/* To be used once the work has been tested to be meet diff1 and has had its
 * nonce adjusted. Returns true if the work target is met. */
bool submit_tested_work(struct thr_info *thr, struct work *work)
{
  struct work *work_out;
  update_work_stats(thr, work);

  if (work->pool->algorithm.type == ALGO_ARGON2D) {
      work_out = copy_work(work);
      submit_work_async(work_out);
      return true;
  }

  if (!fulltest(work->hash, work->target)) {
    applog(LOG_INFO, "%s %d: Share above target", thr->cgpu->drv->name,
           thr->cgpu->device_id);
    return false;
  }
  work_out = copy_work(work);
  submit_work_async(work_out);
  return true;
}

/* Returns true if nonce for work was a valid share */
bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{

  if (work->pool->algorithm.type == ALGO_MTP) {
    struct work *work_out = make_work();
    update_work_stats(thr, work);
    _copy_work(work_out, work, 0);
    submit_work_async(work_out);
    return true;
  }

  if (test_nonce(work, nonce)) {
    submit_tested_work(thr, work);
    return true;
  }

  inc_hw_errors(thr);
  return false;
}

static inline bool abandon_work(struct work *work, struct timeval *wdiff, uint64_t hashes)
{
  if (wdiff->tv_sec > opt_scantime ||
      work->blk.nonce >= MAXTHREADS - hashes ||
      hashes >= 0xfffffffe ||
      stale_work(work, false))
    return true;
  return false;
}

static void mt_disable(struct thr_info *mythr, const int thr_id,
           struct device_drv *drv)
{
  applog(LOG_WARNING, "Thread %d being disabled", thr_id);
  mythr->rolling = mythr->cgpu->rolling = 0;
  applog(LOG_DEBUG, "Waiting on sem in miner thread");
  mythr->paused = true;
  cgsem_wait(&mythr->sem);
  applog(LOG_WARNING, "Thread %d being re-enabled", thr_id);
  mythr->paused = false;
  drv->thread_enable(mythr);
}

/* The main hashing loop for devices that are slow enough to work on one work
 * item at a time, without a queue, aborting work before the entire nonce
 * range has been hashed if needed. */
static void hash_sole_work(struct thr_info *mythr)
{
  const int thr_id = mythr->id;
  struct cgpu_info *cgpu = mythr->cgpu;
  struct device_drv *drv = cgpu->drv;
  struct timeval getwork_start, tv_start, *tv_end, tv_workstart, tv_lastupdate;
  struct sgminer_stats *dev_stats = &(cgpu->sgminer_stats);
  struct sgminer_stats *pool_stats;
  /* Try to cycle approximately 5 times before each log update */
  const long cycle = opt_log_interval / 5 ? 5 : 1;
  const bool primary = mythr->device_thread == 0;
  struct timeval diff, sdiff, wdiff = {0, 0};
  uint32_t max_nonce = drv->can_limit_work(mythr);
  int64_t hashes_done = 0;

  tv_end = &getwork_start;
  cgtime(&getwork_start);
  sdiff.tv_sec = sdiff.tv_usec = 0;
  cgtime(&tv_lastupdate);

  while (likely(!cgpu->shutdown)) {
    struct work *work = get_work(mythr, thr_id);
    int64_t hashes;

    mythr->work_restart = false;
    cgpu->new_work = true;

    cgtime(&tv_workstart);
    work->blk.nonce = 0;
    cgpu->max_hashes = 0;
    if (!drv->prepare_work(mythr, work)) {
      applog(LOG_ERR, "work prepare failed, exiting "
        "mining thread %d", thr_id);
      break;
    }
    work->device_diff = MIN(drv->working_diff, work->work_difficulty);

    /* Dynamically adjust the working diff even if the target
     * diff is very high to ensure we can still validate scrypt is
     * returning shares. */
    double wu;

    wu = total_diff1 / total_secs * 60;
    if (wu > 30 && drv->working_diff < drv->max_diff &&
      drv->working_diff < work->work_difficulty) {
      drv->working_diff++;
      applog(LOG_DEBUG, "Driver %s working diff changed to %.0f",
           drv->dname, drv->working_diff);
      work->device_diff = MIN(drv->working_diff, work->work_difficulty);
    } else if (drv->working_diff > work->work_difficulty)
      drv->working_diff = work->work_difficulty;

    if (work->pool->algorithm.type == ALGO_NEOSCRYPT || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA ||
        work->pool->algorithm.type == ALGO_NEOSCRYPT_NAVI || work->pool->algorithm.type == ALGO_NEOSCRYPT_XAYA_NAVI ||
        work->pool->algorithm.type == ALGO_YESCRYPTR16 || work->pool->algorithm.type == ALGO_YESCRYPTR16_NAVI) {
      set_target_neoscrypt(work->device_target, work->device_diff, work->thr_id);
    } else {
      if (work->pool->algorithm.type == ALGO_ETHASH)
        work->device_diff = MIN(work->sdiff, 60e6);
      set_target(work->device_target, work->device_diff, work->pool->algorithm.diff_multiplier2, work->thr_id);
    }

    if (work->pool->algorithm.type == ALGO_MTP)
		  memcpy(work->device_target, work->pool->Target, 32);

    do {
      cgtime(&tv_start);

      subtime(&tv_start, &getwork_start);

      addtime(&getwork_start, &dev_stats->getwork_wait);
      if (time_more(&getwork_start, &dev_stats->getwork_wait_max))
        copy_time(&dev_stats->getwork_wait_max, &getwork_start);
      if (time_less(&getwork_start, &dev_stats->getwork_wait_min))
        copy_time(&dev_stats->getwork_wait_min, &getwork_start);
      dev_stats->getwork_calls++;

      pool_stats = &(work->pool->sgminer_stats);

      addtime(&getwork_start, &pool_stats->getwork_wait);
      if (time_more(&getwork_start, &pool_stats->getwork_wait_max))
        copy_time(&pool_stats->getwork_wait_max, &getwork_start);
      if (time_less(&getwork_start, &pool_stats->getwork_wait_min))
        copy_time(&pool_stats->getwork_wait_min, &getwork_start);
      pool_stats->getwork_calls++;

      cgtime(&(work->tv_work_start));

      /* Only allow the mining thread to be cancelled when
       * it is not in the driver code. */
      pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

      thread_reportin(mythr);
      hashes = drv->scanhash(mythr, work, work->blk.nonce + max_nonce);
      thread_reportout(mythr);

      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
      pthread_testcancel();

      /* tv_end is == &getwork_start */
      cgtime(&getwork_start);

      if (unlikely(hashes == -1)) {
        applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
        cgpu->deven = DEV_DISABLED;
        dev_error(cgpu, REASON_THREAD_ZERO_HASH);
        event_notify("idle");
        cgpu->shutdown = true;
        break;
      }

      hashes_done += hashes;
      if (hashes > cgpu->max_hashes)
        cgpu->max_hashes = hashes;

      timersub(tv_end, &tv_start, &diff);
      sdiff.tv_sec += diff.tv_sec;
      sdiff.tv_usec += diff.tv_usec;
      if (sdiff.tv_usec > 1000000) {
        ++sdiff.tv_sec;
        sdiff.tv_usec -= 1000000;
      }

      timersub(tv_end, &tv_workstart, &wdiff);

      if (unlikely((long)sdiff.tv_sec < cycle)) {
        int mult;

        if (likely(max_nonce == 0xffffffff))
          continue;

        mult = 1000000 / ((sdiff.tv_usec + 0x400) / 0x400) + 0x10;
        mult *= cycle;
        if (max_nonce > (0xffffffff * 0x400) / mult)
          max_nonce = 0xffffffff;
        else
          max_nonce = (max_nonce * mult) / 0x400;
      } else if (unlikely(sdiff.tv_sec > cycle))
        max_nonce = max_nonce * cycle / sdiff.tv_sec;
      else if (unlikely(sdiff.tv_usec > 100000))
        max_nonce = max_nonce * 0x400 / (((cycle * 1000000) + sdiff.tv_usec) / (cycle * 1000000 / 0x400));

      timersub(tv_end, &tv_lastupdate, &diff);
      /* Update the hashmeter at most 5 times per second */
      if ((hashes_done && (diff.tv_sec > 0 || diff.tv_usec > 200000)) ||
          diff.tv_sec >= opt_log_interval) {
        hashmeter(thr_id, &diff, hashes_done);
        hashes_done = 0;
        copy_time(&tv_lastupdate, tv_end);
      }

      if (unlikely(mythr->work_restart)) {
        /* Apart from device_thread 0, we stagger the
         * starting of every next thread to try and get
         * all devices busy before worrying about
         * getting work for their extra threads */
        if (!primary) {
          struct timespec rgtp;

          rgtp.tv_sec = 0;
          rgtp.tv_nsec = 250 * mythr->device_thread * 1000000;
          nanosleep(&rgtp, NULL);
        }
        break;
      }

      if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
        mt_disable(mythr, thr_id, drv);

      sdiff.tv_sec = sdiff.tv_usec = 0;
    } while (!abandon_work(work, &wdiff, cgpu->max_hashes));
    free_work(work);
  }
  cgpu->deven = DEV_DISABLED;
}

void *miner_thread(void *userdata)
{
  struct thr_info *mythr = (struct thr_info *)userdata;
  const int thr_id = mythr->id;
  struct cgpu_info *cgpu = mythr->cgpu;
  struct device_drv *drv = cgpu->drv;
  char threadname[16];

        snprintf(threadname, sizeof(threadname), "%d/Miner", thr_id);
  RenameThread(threadname);

  thread_reportout(mythr);
  if (!drv->thread_init(mythr)) {
    dev_error(cgpu, REASON_THREAD_FAIL_INIT);
    goto out;
  }

  applog(LOG_DEBUG, "Waiting on sem in miner thread");
  cgsem_wait(&mythr->sem);

  set_highprio();
  drv->hash_work(mythr);
out:
  drv->thread_shutdown(mythr);

  return NULL;
}

enum {
  STAT_SLEEP_INTERVAL   = 1,
  STAT_CTR_INTERVAL   = 10000000,
  FAILURE_INTERVAL    = 30,
};

#ifdef HAVE_LIBCURL
/* Stage another work item from the work returned in a longpoll */
static void convert_to_work(json_t *val, int rolltime, struct pool *pool, struct timeval *tv_lp, struct timeval *tv_lp_reply)
{
  struct work *work;
  bool rc;

  work = make_work();

  rc = work_decode(pool, work, val);
  if (unlikely(!rc)) {
    applog(LOG_ERR, "Could not convert longpoll data to work");
    free_work(work);
    return;
  }
  total_getworks++;
  pool->getwork_requested++;
  work->pool = pool;
  work->rolltime = rolltime;
  copy_time(&work->tv_getwork, tv_lp);
  copy_time(&work->tv_getwork_reply, tv_lp_reply);
  calc_diff(work, 0);

  if (pool->state == POOL_REJECTING)
    work->mandatory = true;

  if (pool->has_gbt)
    gen_gbt_work(pool, work);
  work->longpoll = true;
  work->getwork_mode = GETWORK_MODE_LP;

  /* We'll be checking this work item twice, but we already know it's
   * from a new block so explicitly force the new block detection now
   * rather than waiting for it to hit the stage thread. This also
   * allows testwork to know whether LP discovered the block or not. */
  test_work_current(work);

  /* Don't use backup LPs as work if we have failover-only enabled. Use
   * the longpoll work from a pool that has been rejecting shares as a
   * way to detect when the pool has recovered.
   */
  if (pool != current_pool() && opt_fail_only && pool->state != POOL_REJECTING) {
    free_work(work);
    return;
  }

  work = clone_work(work);

  applog(LOG_DEBUG, "Pushing converted work to stage thread");

  stage_work(work);
  applog(LOG_DEBUG, "Converted longpoll data to work");
}

/* If we want longpoll, enable it for the chosen default pool, or, if
 * the pool does not support longpoll, find the first one that does
 * and use its longpoll support */
static struct pool *select_longpoll_pool(struct pool *cp)
{
  int i;

  if (cp->hdr_path || cp->has_gbt)
    return cp;
  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];

    if (pool->has_stratum || pool->hdr_path)
      return pool;
  }
  return NULL;
}
#endif /* HAVE_LIBCURL */

/* This will make the longpoll thread wait till it's the current pool, or it
 * has been flagged as rejecting, before attempting to open any connections.
 */
static void wait_lpcurrent(struct pool *pool)
{
  while (!cnx_needed(pool) && (pool->state == POOL_DISABLED ||
         (pool != current_pool() && pool_strategy != POOL_LOADBALANCE &&
         pool_strategy != POOL_BALANCE))) {
    mutex_lock(&lp_lock);
    pthread_cond_wait(&lp_cond, &lp_lock);
    mutex_unlock(&lp_lock);
  }
}

#ifdef HAVE_LIBCURL
static void *longpoll_thread(void *userdata)
{
  struct pool *cp = (struct pool *)userdata;
  /* This *pool is the source of the actual longpoll, not the pool we've
   * tied it to */
  struct timeval start, reply, end;
  struct pool *pool = NULL;
  char threadname[16];
  CURL *curl = NULL;
  char curl_err_str[CURL_ERROR_SIZE];
  int failures = 0;
  char lpreq[1024];
  char *lp_url;
  int rolltime;

  snprintf(threadname, sizeof(threadname), "%d/Longpoll", cp->pool_no);
  RenameThread(threadname);

  curl = curl_easy_init();
  if (unlikely(!curl)) {
    applog(LOG_ERR, "CURL initialisation failed");
    return NULL;
  }

retry_pool:
  pool = select_longpoll_pool(cp);
  if (!pool) {
    applog(LOG_WARNING, "No suitable long-poll found for %s", cp->rpc_url);
    while (!pool) {
      cgsleep_ms(60000);
      pool = select_longpoll_pool(cp);
    }
  }

  if (pool->has_stratum) {
    applog(LOG_WARNING, "Block change for %s detection via %s stratum",
           cp->rpc_url, pool->rpc_url);
    goto out;
  }

  /* Any longpoll from any pool is enough for this to be true */
  have_longpoll = true;

  wait_lpcurrent(cp);

  if (pool->has_gbt) {
    lp_url = pool->rpc_url;
    applog(LOG_WARNING, "GBT longpoll ID activated for %s", lp_url);
  } else {
    strcpy(lpreq, getwork_req);

    lp_url = pool->lp_url;
    if (cp == pool)
      applog(LOG_WARNING, "Long-polling activated for %s", lp_url);
    else
      applog(LOG_WARNING, "Long-polling activated for %s via %s", cp->rpc_url, lp_url);
  }

  while (42) {
    json_t *val, *soval;

    wait_lpcurrent(cp);

    cgtime(&start);

    /* Update the longpollid every time, but do it under lock to
     * avoid races */
    if (pool->has_gbt) {
      cg_rlock(&pool->gbt_lock);
      snprintf(lpreq, sizeof(lpreq),
        "{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": "
        "[{\"capabilities\": [\"coinbasetxn\", \"workid\", \"coinbase/append\"], "
        "\"longpollid\": \"%s\"}]}\n", pool->longpollid);
      cg_runlock(&pool->gbt_lock);
    }

    /* Longpoll connections can be persistent for a very long time
     * and any number of issues could have come up in the meantime
     * so always establish a fresh connection instead of relying on
     * a persistent one. */
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
    val = json_rpc_call(curl, curl_err_str, lp_url, pool->rpc_userpass,
            lpreq, false, true, &rolltime, pool, false);

    cgtime(&reply);

    if (likely(val)) {
      soval = json_object_get(json_object_get(val, "result"), "submitold");
      if (soval)
        pool->submit_old = json_is_true(soval);
      else
        pool->submit_old = false;
      convert_to_work(val, rolltime, pool, &start, &reply);
      failures = 0;
      json_decref(val);
    } else {
      /* Some pools regularly drop the longpoll request so
       * only see this as longpoll failure if it happens
       * immediately and just restart it the rest of the
       * time. */
      cgtime(&end);
      if (end.tv_sec - start.tv_sec > 30)
        continue;
      if (failures == 1)
        applog(LOG_WARNING, "longpoll failed for %s, retrying every 30s", lp_url);
      cgsleep_ms(30000);
    }

    if (pool != cp) {
      pool = select_longpoll_pool(cp);
      if (pool->has_stratum) {
        applog(LOG_WARNING, "Block change for %s detection via %s stratum",
               cp->rpc_url, pool->rpc_url);
        break;
      }
      if (unlikely(!pool))
        goto retry_pool;
    }

    if (unlikely(pool->removed))
      break;
  }

out:
  curl_easy_cleanup(curl);

  return NULL;
}
#else /* HAVE_LIBCURL */
static void *longpoll_thread(void __maybe_unused *userdata)
{
  pthread_detach(pthread_self());
  return NULL;
}
#endif /* HAVE_LIBCURL */

void reinit_device(struct cgpu_info *cgpu)
{
  mutex_lock(&algo_switch_lock);
  cgpu->drv->reinit_device(cgpu);
  mutex_unlock(&algo_switch_lock);
}

static struct timeval rotate_tv;

/* We reap curls if they are unused for over a minute */
static void reap_curl(struct pool *pool)
{
  struct curl_ent *ent, *iter;
  struct timeval now;
  int reaped = 0;

  cgtime(&now);

  mutex_lock(&pool->pool_lock);
  list_for_each_entry_safe(ent, iter, &pool->curlring, node) {
    if (pool->curls < 2)
      break;
    if (now.tv_sec - ent->tv.tv_sec > 300) {
      reaped++;
      pool->curls--;
      list_del(&ent->node);
      curl_easy_cleanup(ent->curl);
      free(ent);
    }
  }
  mutex_unlock(&pool->pool_lock);

  if (reaped)
    applog(LOG_DEBUG, "Reaped %d curl%s from %s", reaped, reaped > 1 ? "s" : "", get_pool_name(pool));
}

static void *watchpool_thread(void __maybe_unused *userdata)
{
  int intervals = 0;

  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  RenameThread("Watchpool");

  set_lowprio();

  while (42) {
    struct timeval now;
    int sleeptimeout/* = 1000*/;
    int i;

    // get current time
    cgtime(&now);

    // sleep timeout is 30 secs for most cases
    sleeptimeout = opt_watchpool_refresh * 1000;

    //limit to 5 secs minimum...
    if (sleeptimeout < 5000) {
      sleeptimeout = 5000;
    }

    // check the status of each pool
    for (i = 0; i < total_pools; ++i) {
      struct pool *pool = pools[i];

      reap_curl(pool);

      /* Get a rolling utility per pool over 10 mins */
      if (intervals >= 600) {
        int shares = pool->diff1 - pool->last_shares;

        pool->last_shares = pool->diff1;
        pool->utility = (pool->utility + (double)shares * 0.63) / 1.63;
        pool->shares = pool->utility;
        intervals = 0;
      }

      // if this pool is disabled, skip it
      if (pool->state == POOL_DISABLED) {
        continue;
      }

      /* Don't start testing any pools if the test threads
       * from startup are still doing their first attempt. */
      if (unlikely(pool->testing)) {
        pthread_join(pool->test_thread, NULL);
        pool->testing = false;
      }

      /* Test pool is idle once every minute */
      if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 30) {
        cgtime(&pool->tv_idle);
        if (pool_active(pool, true) && pool_tclear(pool, &pool->idle)) {
          pool_resus(pool);
        }
      }

      // if this pool is alive and the priority is greater (lower) than currently connected pool
      if (!pool->idle && pool->prio < cp_prio()) {
        // failover strategy - switch when failover delay is met
        if (pool_strategy == POOL_FAILOVER && (now.tv_sec - pool->tv_idle.tv_sec > opt_fail_switch_delay)) {
          applog(LOG_WARNING, "%s stable for %d seconds", get_pool_name(pool), opt_fail_switch_delay);
          switch_pools(NULL);
        }
      }

    } //end pool loop

    // if the pool stategy is rotation and we have been over the rotate delay, switch pool
    if (pool_strategy == POOL_ROTATE && now.tv_sec - rotate_tv.tv_sec > 60 * opt_rotate_period) {
      cgtime(&rotate_tv);
      switch_pools(NULL);
    }

    // if the current pool is dead/idle switch pool
    if (current_pool()->idle) {
      switch_pools(NULL);
    }

    cgsleep_ms(sleeptimeout);
    intervals += (sleeptimeout / 1000);

  } //end main loop

  return NULL;
}

/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
#define WATCHDOG_INTERVAL   2
#define WATCHDOG_SICK_TIME    120
#define WATCHDOG_DEAD_TIME    600
#define WATCHDOG_SICK_COUNT   (WATCHDOG_SICK_TIME/WATCHDOG_INTERVAL)
#define WATCHDOG_DEAD_COUNT   (WATCHDOG_DEAD_TIME/WATCHDOG_INTERVAL)

static void *watchdog_thread(void __maybe_unused *userdata)
{
  const unsigned int interval = WATCHDOG_INTERVAL;
  struct timeval zero_tv;

  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  RenameThread("Watchdog");

  set_lowprio();
  memset(&zero_tv, 0, sizeof(struct timeval));
  cgtime(&rotate_tv);

  while (1) {
    int i;
    struct timeval now;

    sleep(interval);

    discard_stale();

    hashmeter(-1, &zero_tv, 0);

    rd_lock(&mining_thr_lock);

#ifdef HAVE_CURSES
    if (curses_active_locked()) {
      struct cgpu_info *cgpu;
      int count;

      change_logwinsize();
      curses_print_status();

      if (!opt_compact) {
        count = 0;
        for (i = 0; i < total_devices; i++) {
          cgpu = get_devices(i);
          if (cgpu && (!opt_removedisabled || cgpu->deven != DEV_DISABLED || devices_enabled[i]))
            curses_print_devstatus(cgpu, count++);
        }
      }

      touchwin(statuswin);
      wrefresh(statuswin);
      touchwin(logwin);
      wrefresh(logwin);
      unlock_curses();
    }
#endif

    cgtime(&now);

    // check last getwork time if greater than 10 mins, declare idle...
    if ((time(NULL) - last_getwork) >= 600) {
      event_notify("idle");
    }

    if (!sched_paused && !should_run()) {
      applog(LOG_WARNING, "Pausing execution as per stop time %02d:%02d scheduled",
             schedstop.tm.tm_hour, schedstop.tm.tm_min);
      if (!schedstart.enable) {
        quit(0, "Terminating execution as planned");
        break;
      }

      applog(LOG_WARNING, "Will restart execution as scheduled at %02d:%02d",
             schedstart.tm.tm_hour, schedstart.tm.tm_min);
      sched_paused = true;

      for (i = 0; i < mining_threads; i++)
        mining_thr[i]->pause = true;
    } else if (sched_paused && should_run()) {
      applog(LOG_WARNING, "Restarting execution as per start time %02d:%02d scheduled",
        schedstart.tm.tm_hour, schedstart.tm.tm_min);
      if (schedstop.enable)
        applog(LOG_WARNING, "Will pause execution as scheduled at %02d:%02d",
          schedstop.tm.tm_hour, schedstop.tm.tm_min);
      sched_paused = false;

      for (i = 0; i < mining_threads; i++) {
        struct thr_info *thr;

        thr = mining_thr[i];

        /* Don't touch disabled devices */
        if (thr->cgpu->deven == DEV_DISABLED)
          continue;
        thr->pause = false;
        applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
        cgsem_post(&thr->sem);
      }
    }

    for (i = 0; i < total_devices; ++i) {
      struct cgpu_info *cgpu = get_devices(i);
      struct thr_info *thr = cgpu->thr[0];
      enum dev_enable *denable;
      char dev_str[8];
      int gpu;

      cgpu->drv->get_stats(cgpu);

      gpu = cgpu->device_id;
      denable = &cgpu->deven;
      snprintf(dev_str, sizeof(dev_str), "%s%d", cgpu->drv->name, gpu);

#ifdef HAVE_ADL
      if (adl_active && cgpu->has_adl)
        gpu_autotune(gpu, denable);
      if (opt_debug && cgpu->has_adl) {
        int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
        float temp = 0, vddc = 0;

        if (gpu_stats(gpu, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune))
          applog(LOG_DEBUG, "%.1f C  F: %d%%(%dRPM)  E: %dMHz  M: %dMhz  V: %.3fV  A: %d%%  P: %d%%",
          temp, fanpercent, fanspeed, engineclock, memclock, vddc, activity, powertune);
      }
#endif

      /* Thread is disabled or waiting on getwork */
      if (*denable == DEV_DISABLED || thr->getwork)
        continue;

      if (cgpu->status != LIFE_WELL && (now.tv_sec - thr->last.tv_sec < WATCHDOG_SICK_TIME)) {
        if (cgpu->status != LIFE_INIT)
        applog(LOG_ERR, "%s: Recovered, declaring WELL!", dev_str);
        cgpu->status = LIFE_WELL;
        cgpu->device_last_well = time(NULL);
      } else if (cgpu->status == LIFE_WELL && (now.tv_sec - thr->last.tv_sec > WATCHDOG_SICK_TIME)) {
        thr->rolling = cgpu->rolling = 0;
        cgpu->status = LIFE_SICK;
        applog(LOG_ERR, "%s: Idle for more than 2 minutes, declaring SICK!", dev_str);
        cgtime(&thr->sick);

        dev_error(cgpu, REASON_DEV_SICK_IDLE_60);
        event_notify("gpu_sick");

#ifdef HAVE_ADL
        if (adl_active && cgpu->has_adl && gpu_activity(gpu) > 50) {
          applog(LOG_ERR, "GPU still showing activity suggesting a hard hang.");
          applog(LOG_ERR, "Will not attempt to auto-restart it.");
        } else
#endif
        if (opt_restart) {
          applog(LOG_ERR, "%s: Attempting to restart", dev_str);
          reinit_device(cgpu);
        }
      } else if (cgpu->status == LIFE_SICK && (now.tv_sec - thr->last.tv_sec > WATCHDOG_DEAD_TIME)) {
        cgpu->status = LIFE_DEAD;
        applog(LOG_ERR, "%s: Not responded for more than 10 minutes, declaring DEAD!", dev_str);
        cgtime(&thr->sick);

        dev_error(cgpu, REASON_DEV_DEAD_IDLE_600);
        event_notify("gpu_dead");
      } else if (now.tv_sec - thr->sick.tv_sec > 60 &&
           (cgpu->status == LIFE_SICK || cgpu->status == LIFE_DEAD)) {
        /* Attempt to restart a GPU that's sick or dead once every minute */
        cgtime(&thr->sick);
#ifdef HAVE_ADL
        if (adl_active && cgpu->has_adl && gpu_activity(gpu) > 50) {
          /* Again do not attempt to restart a device that may have hard hung */
        } else
#endif
        if (opt_restart)
          reinit_device(cgpu);
      }
    }
    rd_unlock(&mining_thr_lock);
  }

  return NULL;
}

static void log_print_status(struct cgpu_info *cgpu)
{
  char logline[255];

  get_statline(logline, sizeof(logline), cgpu);
  applog(LOG_WARNING, "%s", logline);
}

static void noop_get_statline(char __maybe_unused *buf, size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu);
void blank_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info __maybe_unused *cgpu);

void print_summary(void)
{
  struct timeval diff;
  int hours, mins, secs, i;
  double utility, displayed_hashes, work_util;
  bool mhash_base = true;

  timersub(&total_tv_end, &total_tv_start, &diff);
  hours = diff.tv_sec / 3600;
  mins = (diff.tv_sec % 3600) / 60;
  secs = diff.tv_sec % 60;

  utility = total_accepted / total_secs * 60;
  work_util = total_diff1 / total_secs * 60;

  applog(LOG_WARNING, "\nSummary of runtime statistics:\n");
  applog(LOG_WARNING, "Started at %s", datestamp);
  if (total_pools == 1)
    applog(LOG_WARNING, "Pool: %s", pools[0]->rpc_url);
  applog(LOG_WARNING, "Runtime: %d hrs : %d mins : %d secs", hours, mins, secs);
  displayed_hashes = total_mhashes_done / total_secs;
  if (displayed_hashes < 1) {
    displayed_hashes *= 1000;
    mhash_base = false;
  }

  applog(LOG_WARNING, "Average hashrate: %.1f %shash/s", displayed_hashes, mhash_base? "Mega" : "Kilo");
  applog(LOG_WARNING, "Solved blocks: %d", found_blocks);
  applog(LOG_WARNING, "Best share difficulty: %s", best_share);
  applog(LOG_WARNING, "Share submissions: %d", total_accepted + total_rejected);
  applog(LOG_WARNING, "Accepted shares: %d", total_accepted);
  applog(LOG_WARNING, "Rejected shares: %d", total_rejected);
  applog(LOG_WARNING, "Accepted difficulty shares: %1.f", total_diff_accepted);
  applog(LOG_WARNING, "Rejected difficulty shares: %1.f", total_diff_rejected);
  if (total_accepted || total_rejected)
    applog(LOG_WARNING, "Reject ratio: %.1f%%", (double)(total_rejected * 100) / (double)(total_accepted + total_rejected));
  applog(LOG_WARNING, "Hardware errors: %d", hw_errors);
  applog(LOG_WARNING, "Utility (accepted shares / min): %.2f/min", utility);
  applog(LOG_WARNING, "Work Utility (diff1 shares solved / min): %.2f/min\n", work_util);

  applog(LOG_WARNING, "Stale submissions discarded due to new blocks: %d", total_stale);
  applog(LOG_WARNING, "Unable to get work from server occasions: %d", total_go);
  applog(LOG_WARNING, "Work items generated locally: %d", local_work);
  applog(LOG_WARNING, "Submitting work remotely delay occasions: %d", total_ro);
  applog(LOG_WARNING, "New blocks detected on network: %d\n", new_blocks);

  if (total_pools > 1) {
    for (i = 0; i < total_pools; i++) {
      struct pool *pool = pools[i];

      applog(LOG_WARNING, "Pool: %s", pool->rpc_url);
      if (pool->solved)
        applog(LOG_WARNING, "SOLVED %d BLOCK%s!", pool->solved, pool->solved > 1 ? "S" : "");
      applog(LOG_WARNING, " Share submissions: %d", pool->accepted + pool->rejected);
      applog(LOG_WARNING, " Accepted shares: %d", pool->accepted);
      applog(LOG_WARNING, " Rejected shares: %d", pool->rejected);
      applog(LOG_WARNING, " Accepted difficulty shares: %1.f", pool->diff_accepted);
      applog(LOG_WARNING, " Rejected difficulty shares: %1.f", pool->diff_rejected);
      if (pool->accepted || pool->rejected)
        applog(LOG_WARNING, " Reject ratio: %.1f%%", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));

      applog(LOG_WARNING, " Items worked on: %d", pool->works);
      applog(LOG_WARNING, " Stale submissions discarded due to new blocks: %d", pool->stale_shares);
      applog(LOG_WARNING, " Unable to get work from server occasions: %d", pool->getfail_occasions);
      applog(LOG_WARNING, " Submitting work remotely delay occasions: %d\n", pool->remotefail_occasions);
    }
  }

  applog(LOG_WARNING, "Summary of per device statistics:\n");
  for (i = 0; i < total_devices; ++i) {
    struct cgpu_info *cgpu = get_devices(i);

    cgpu->drv->get_statline_before = &blank_get_statline_before;
    cgpu->drv->get_statline = &noop_get_statline;
    log_print_status(cgpu);
  }

  if (opt_shares) {
    applog(LOG_WARNING, "Mined %.0f accepted shares of %d requested\n", total_diff_accepted, opt_shares);
    if (opt_shares > total_diff_accepted)
      applog(LOG_WARNING, "WARNING - Mined only %.0f shares of %d requested.", total_diff_accepted, opt_shares);
  }
  applog(LOG_WARNING, " ");

  fflush(stderr);
  fflush(stdout);
}

static void clean_up(bool restarting)
{
#ifdef HAVE_ADL
  clear_adl(nDevs);
#endif
  cgtime(&total_tv_end);
#ifdef WIN32
  timeEndPeriod(1);
#endif
#ifdef HAVE_CURSES
  disable_curses();
#endif
  if (!restarting && !opt_realquiet && successful_connect)
    print_summary();

  curl_global_cleanup();
}

void _quit(int status)
{
  clean_up(false);

#if defined(unix) || defined(__APPLE__)
  if (forkpid > 0) {
    kill(forkpid, SIGTERM);
    forkpid = 0;
  }
#endif

  exit(status);
}

#ifdef HAVE_CURSES
char *curses_input(const char *query)
{
  char *input;

  echo();
  input = (char *)malloc(255);
  if (!input)
    quit(1, "Failed to malloc input");
  leaveok(logwin, false);
  wlogprint("%s:\n", query);
  wgetnstr(logwin, input, 255);
  if (!strlen(input))
    strcpy(input, "-1");
  leaveok(logwin, true);
  noecho();
  return input;
}
#endif

static bool pools_active = false;

static void *test_pool_thread(void *arg)
{
  struct pool *pool = (struct pool *)arg;

  if (pool_active(pool, false)) {
    pool_tset(pool, &pool->lagging);
    pool_tclear(pool, &pool->idle);
    bool first_pool = false;

    cg_wlock(&control_lock);
    if (!pools_active) {
      currentpool = pool;
      if (pool->pool_no != 0)
        first_pool = true;
      pools_active = true;
    }
    cg_wunlock(&control_lock);

    if (unlikely(first_pool))
      applog(LOG_NOTICE, "Switching to %s - first alive pool", get_pool_name(pool));

    pool_resus(pool);
    switch_pools(NULL);
  } else {
    pool_died(pool);
  }

  return NULL;
}

/* Always returns true that the pool details were added unless we are not
 * live, implying this is the only pool being added, so if no pools are
 * active it returns false. */
bool add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass, char *name, char *desc, char *profile, char *algo)
{
  size_t siz;

  url = get_proxy(url, pool);

  pool->rpc_url = url;
  pool->rpc_user = user;
  pool->rpc_pass = pass;
  pool->name = name;
  pool->description = desc;
  pool->profile = profile;

    //if a profile was supplied, apply pool properties from profile
    if(!empty_string(profile))
        apply_pool_profile(pool);    //remove profile if was invalid

    //if profile is empty, assign algorithm or default algorithm
    if(empty_string(pool->profile))
    {
        if(!empty_string(algo))
            set_algorithm(&pool->algorithm, algo);
        else
            set_algorithm(&pool->algorithm, default_profile.algorithm.name);
    }

  siz = strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2;
  pool->rpc_userpass = (char *)malloc(siz);
  if (!pool->rpc_userpass)
    quit(1, "Failed to malloc userpass");
  snprintf(pool->rpc_userpass, siz, "%s:%s", pool->rpc_user, pool->rpc_pass);

  pool->testing = true;
  pool->idle = true;
  enable_pool(pool);

  pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
  if (!live) {
    pthread_join(pool->test_thread, NULL);
    pool->testing = false;
    return pools_active;
  }
  return true;
}

#ifdef HAVE_CURSES
static bool input_pool(bool live)
{
  char *url = NULL, *user = NULL, *pass = NULL;
  char *name = NULL, *desc = NULL, *profile = NULL, *algo = NULL;
  struct pool *pool;
  bool ret = false;

  immedok(logwin, true);
  wlogprint("Input server details.\n");

  /* Get user input */
  url = curses_input("URL");
  if (!url) goto out;
  user = curses_input("User name");
  if (!user) goto out;
  pass = curses_input("Password");
  if (!pass) goto out;
  name = curses_input("Pool name (optional)");
  if (strcmp(name, "-1") == 0) strcpy(name, "");
  desc = curses_input("Description (optional)");
  if (strcmp(desc, "-1") == 0) strcpy(desc, "");
  profile = curses_input("Profile (optional)");
  if (strcmp(profile, "-1") == 0) profile[0] = '\0';
  algo = curses_input("Algorithm (optional)");
  if (strcmp(algo, "-1") == 0) algo[0] = '\0';

  pool = add_pool();

  if (!detect_stratum(pool, url) && strncmp(url, "http://", 7) &&
      strncmp(url, "https://", 8)) {
    char *httpinput;

    httpinput = (char *)malloc(256);
    if (!httpinput)
      quit(1, "Failed to malloc httpinput");
    strcpy(httpinput, "http://");
    strncat(httpinput, url, 248);
    free(url);
    url = httpinput;
  }

  ret = add_pool_details(pool, live, url, user, pass,
             name, desc, profile, algo);
out:
  immedok(logwin, false);

  if (!ret) {
    if (url)
      free(url);
    if (user)
      free(user);
    if (pass)
      free(pass);
    if (name)
      free(name);
    if (desc)
      free(desc);
    if (algo)
      free(algo);
  }
  return ret;
}
#endif

#if defined(unix) || defined(__APPLE__)
static void fork_monitor()
{
  // Make a pipe: [readFD, writeFD]
  int pfd[2];
  int r = pipe(pfd);

  if (r < 0) {
    perror("pipe - failed to create pipe for --monitor");
    exit(1);
  }

  // Make stderr write end of pipe
  fflush(stderr);
  r = dup2(pfd[1], 2);
  if (r < 0) {
    perror("dup2 - failed to alias stderr to write end of pipe for --monitor");
    exit(1);
  }
  r = close(pfd[1]);
  if (r < 0) {
    perror("close - failed to close write end of pipe for --monitor");
    exit(1);
  }

  // Don't allow a dying monitor to kill the main process
  sighandler_t sr0 = signal(SIGPIPE, SIG_IGN);
  sighandler_t sr1 = signal(SIGPIPE, SIG_IGN);
  if (SIG_ERR == sr0 || SIG_ERR == sr1) {
    perror("signal - failed to edit signal mask for --monitor");
    exit(1);
  }

  // Fork a child process
  forkpid = fork();
  if (forkpid < 0) {
    perror("fork - failed to fork child process for --monitor");
    exit(1);
  }

  // Child: launch monitor command
  if (0 == forkpid) {
    // Make stdin read end of pipe
    r = dup2(pfd[0], 0);
    if (r < 0) {
      perror("dup2 - in child, failed to alias read end of pipe to stdin for --monitor");
      exit(1);
    }
    close(pfd[0]);
    if (r < 0) {
      perror("close - in child, failed to close read end of  pipe for --monitor");
      exit(1);
    }

    // Launch user specified command
    execl("/bin/bash", "/bin/bash", "-c", opt_stderr_cmd, (char*)NULL);
    perror("execl - in child failed to exec user specified command for --monitor");
    exit(1);
  }

  // Parent: clean up unused fds and bail
  r = close(pfd[0]);
  if (r < 0) {
    perror("close - failed to close read end of pipe for --monitor");
    exit(1);
  }
}
#endif // defined(unix)

#ifdef HAVE_CURSES
static void enable_curses_windows(void)
{
  int x,y;

  getmaxyx(mainwin, y, x);
  statuswin = newwin(logstart, x, 0, 0);
  leaveok(statuswin, true);
  logwin = newwin(y - logcursor, 0, logcursor, 0);
  idlok(logwin, true);
  scrollok(logwin, true);
  leaveok(logwin, true);
  cbreak();
  noecho();
}
void enable_curses(void) {
  lock_curses();
  if (curses_active) {
    unlock_curses();
    return;
  }

  mainwin = initscr();
  enable_curses_windows();
  curses_active = true;
  statusy = logstart;
  unlock_curses();
}
#endif

/* Various noop functions for drivers that don't support or need their
 * variants. */
static void noop_reinit_device(struct cgpu_info __maybe_unused *cgpu)
{
}

void blank_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info __maybe_unused *cgpu)
{
  tailsprintf(buf, bufsiz, "               | ");
}

static void noop_get_statline(char __maybe_unused *buf, size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu)
{
}

static bool noop_get_stats(struct cgpu_info __maybe_unused *cgpu)
{
  return true;
}

static bool noop_thread_prepare(struct thr_info __maybe_unused *thr)
{
  return true;
}

static uint64_t noop_can_limit_work(struct thr_info __maybe_unused *thr)
{
  return 0xffffffff;
}

static bool noop_thread_init(struct thr_info __maybe_unused *thr)
{
  return true;
}

static bool noop_prepare_work(struct thr_info __maybe_unused *thr, struct work __maybe_unused *work)
{
  return true;
}

static void noop_hw_error(struct thr_info __maybe_unused *thr)
{
}

static void noop_thread_shutdown(struct thr_info __maybe_unused *thr)
{
}

static void noop_thread_enable(struct thr_info __maybe_unused *thr)
{
}

static void noop_detect(void)
{
}
#define noop_flush_work noop_reinit_device
#define noop_update_work noop_reinit_device
#define noop_queue_full noop_get_stats
#define noop_zero_stats noop_reinit_device

/* Fill missing driver drv functions with noops */
void fill_device_drv(struct device_drv *drv)
{
  if (!drv->drv_detect)
    drv->drv_detect = &noop_detect;
  if (!drv->reinit_device)
    drv->reinit_device = &noop_reinit_device;
  if (!drv->get_statline_before)
    drv->get_statline_before = &blank_get_statline_before;
  if (!drv->get_statline)
    drv->get_statline = &noop_get_statline;
  if (!drv->get_stats)
    drv->get_stats = &noop_get_stats;
  if (!drv->thread_prepare)
    drv->thread_prepare = &noop_thread_prepare;
  if (!drv->can_limit_work)
    drv->can_limit_work = &noop_can_limit_work;
  if (!drv->thread_init)
    drv->thread_init = &noop_thread_init;
  if (!drv->prepare_work)
    drv->prepare_work = &noop_prepare_work;
  if (!drv->hw_error)
    drv->hw_error = &noop_hw_error;
  if (!drv->thread_shutdown)
    drv->thread_shutdown = &noop_thread_shutdown;
  if (!drv->thread_enable)
    drv->thread_enable = &noop_thread_enable;
  if (!drv->hash_work)
    drv->hash_work = &hash_sole_work;
  if (!drv->flush_work)
    drv->flush_work = &noop_flush_work;
  if (!drv->update_work)
    drv->update_work = &noop_update_work;
  if (!drv->queue_full)
    drv->queue_full = &noop_queue_full;
  if (!drv->zero_stats)
    drv->zero_stats = &noop_zero_stats;
  if (!drv->max_diff)
    drv->max_diff = 1;
  if (!drv->working_diff)
    drv->working_diff = 1;
}

void enable_device(int i)
{
  rd_lock(&devices_lock);
  devices[i]->deven = DEV_ENABLED;
  rd_unlock(&devices_lock);
}

struct _cgpu_devid_counter {
  char name[4];
  int lastid;
  UT_hash_handle hh;
};

static void adjust_mostdevs(void)
{
  int i;

  most_devices = 0;
  for (i = 0; i < total_devices; i++) {
      if (devices_enabled[i]) {
        most_devices++;
      }
  }
}

bool add_cgpu(struct cgpu_info *cgpu)
{
  static struct _cgpu_devid_counter *devids = NULL;
  struct _cgpu_devid_counter *d;

  HASH_FIND_STR(devids, cgpu->drv->name, d);
  if (d)
    cgpu->device_id = ++d->lastid;
  else {
    d = (struct _cgpu_devid_counter *)malloc(sizeof(*d));
    memcpy(d->name, cgpu->drv->name, sizeof(d->name));
    cgpu->device_id = d->lastid = 0;
    HASH_ADD_STR(devids, name, d);
  }

  wr_lock(&devices_lock);
  devices = (struct cgpu_info **)realloc(devices, sizeof(struct cgpu_info *) * (total_devices + 2));
  wr_unlock(&devices_lock);

  mutex_lock(&stats_lock);
  cgpu->last_device_valid_work = time(NULL);
  mutex_unlock(&stats_lock);

  wr_lock(&devices_lock);
  devices[total_devices++] = cgpu;
  wr_unlock(&devices_lock);

  cgpu->eth_dag.current_epoch = UINT32_MAX;
  cglock_init(&cgpu->eth_dag.lock);

  adjust_mostdevs();
  return true;
}

static void probe_pools(void)
{
  int i;

  for (i = 0; i < total_pools; i++)
  {
    struct pool *pool = pools[i];

    pool->testing = true;
    pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
  }
}

static void restart_mining_threads(unsigned int new_n_threads)
{
  struct thr_info *thr;
  unsigned int i, j, k;

  // Stop and free threads
  if (mining_thr)
  {
    rd_lock(&mining_thr_lock);
    for (i = 0; i < mining_threads; i++)
    {
        applog(LOG_DEBUG, "Shutting down thread %d", i);
        mining_thr[i]->cgpu->shutdown = true;
    }
    rd_unlock(&mining_thr_lock);

    // kill_mining will rd lock mining_thr_lock
    kill_mining();

    applog(LOG_DEBUG, "Finish switching pools");
    // Finish switching pools
    mutex_lock(&algo_switch_wait_lock);
    algo_switch_n = 0;
    mutex_unlock(&algo_switch_wait_lock);

    applog(LOG_DEBUG, "Shutdown OpenCL contexts...");
    rd_lock(&mining_thr_lock);
    for (i = 0; i < mining_threads; i++)
    {
        thr = mining_thr[i];
        thr->cgpu->drv->thread_shutdown(thr);
        thr->cgpu->shutdown = false;
    }
    rd_unlock(&mining_thr_lock);
  }

  wr_lock(&mining_thr_lock);

  if (mining_thr)
  {
    applog(LOG_DEBUG, "Free old mining and device thread memory...");
    rd_lock(&devices_lock);
    for (i = 0; i < total_devices; i++) {
      if (devices[i]->thr) free(devices[i]->thr);
    }
    rd_unlock(&devices_lock);

    for (i = 0; i < mining_threads; i++) {
      free(mining_thr[i]);
    }

    free(mining_thr);
  }

  // Alloc
  applog(LOG_DEBUG, "Allocate new threads...");
  mining_threads = (int) new_n_threads;
#ifdef HAVE_CURSES
  adj_width(mining_threads, &dev_width);
#endif
  mining_thr = (struct thr_info **)calloc(mining_threads, sizeof(thr));
  if (!mining_thr)
    quit(1, "Failed to calloc mining_thr");
  for (i = 0; i < mining_threads; i++) {
    mining_thr[i] = (struct thr_info *)calloc(1, sizeof(struct thr_info));
    if (!mining_thr[i])
      quit(1, "Failed to calloc mining_thr[%d]", i);
  }

  rd_lock(&devices_lock);

  // Start threads
  struct pool *pool;

  if(gpu_initialized)
    pool = current_pool();
  else
    pool = pools[init_pool];

  k = 0;
  for (i = 0; i < total_devices; ++i) {
    struct cgpu_info *cgpu = devices[i];

    cgpu->thr = (struct thr_info **)malloc(sizeof(struct thr_info *) * (cgpu->threads+1));
    cgpu->thr[cgpu->threads] = NULL;
    cgpu->status = LIFE_INIT;

    if (cgpu->deven == DEV_DISABLED && opt_removedisabled) {
      cgpu->threads = 0;
      continue;
    }

    applog(LOG_DEBUG, "Assign threads for device %d", i);
    for (j = 0; j < cgpu->threads; ++j, ++k)
    {
      thr = mining_thr[k];
      thr->id = k;
      thr->pool_no = pool->pool_no;
      applog(LOG_DEBUG, "Thread %d set pool = %d (%s)", k, thr->pool_no, isnull(get_pool_name(pools[thr->pool_no]), ""));
      thr->cgpu = cgpu;
      thr->device_thread = j;

      cgtime(&thr->last);
      cgpu->thr[j] = thr;

      if (!cgpu->drv->thread_prepare(thr)) {
        applog(LOG_ERR, "thread_prepare failed for thread %d", thr->id);
        continue;
      }
    }
  }
  rd_unlock(&devices_lock);
  wr_unlock(&mining_thr_lock);

  rd_lock(&devices_lock);
  for (i = 0; i < total_devices; ++i) {
    struct cgpu_info *cgpu = devices[i];

    for (j = 0; j < cgpu->threads; ++j) {
      thr = cgpu->thr[j];

      applog(LOG_DEBUG, "Starting device %d mining thread %d...", i, j);
      if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
        quit(1, "thread %d create failed", thr->id);

      /* Enable threads for devices set not to mine but disable
       * their queue in case we wish to enable them later */
      if (cgpu->deven != DEV_DISABLED) {
        applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
        cgsem_post(&thr->sem);
      }
    }
  }
  rd_unlock(&devices_lock);
}

static void *restart_mining_threads_thread(void *userdata)
{
  //get thread id
  pthread_t t = pthread_self();

  //detach
  pthread_detach(t);

  //restart mining threads
  restart_mining_threads((unsigned int) (intptr_t) userdata);

  return NULL;
}

#define DRIVER_FILL_DEVICE_DRV(X) fill_device_drv(&X##_drv);

static void set_current_pool(struct pool *pool) {
  applog(LOG_DEBUG, "Trying to set current pool...");
  bool free_dag = (currentpool != NULL && currentpool->algorithm.type == ALGO_ETHASH && pool->algorithm.type != ALGO_ETHASH);
  if (free_dag) {
    cg_wlock(&currentpool->data_lock);
    eth_cache_t *cache = &currentpool->eth_cache;
    cache->disabled = true;
    for (int i = 0; i < cache->nDevs; i++) {
      cg_wlock(&cache->dags[i]->lock);
      if (cache->dags[i]->dag_buffer != NULL)
        clReleaseMemObject(cache->dags[i]->dag_buffer);
      cache->dags[i]->dag_buffer = NULL;
      cache->dags[i]->pool = NULL;
      cache->dags[i]->max_epoch = UINT32_MAX;
      cache->dags[i]->current_epoch = UINT32_MAX;
      cg_wunlock(&cache->dags[i]->lock);
    }
    free(cache->dags);
    cache->dags = NULL;
    cache->nDevs = 0;
    cg_wunlock(&currentpool->data_lock);
  }
  currentpool = pool;

  cg_wlock(&currentpool->data_lock);
  if (currentpool->algorithm.type == ALGO_ETHASH) 
    currentpool->eth_cache.disabled = false;
  cg_wunlock(&currentpool->data_lock);
}

int main(int argc, char *argv[])
{
#ifndef _MSC_VER
  struct sigaction handler;
#endif
  struct thr_info *thr;
  struct block *block;
  int i;
  char *s;

  /* This dangerous function tramples random dynamically allocated
   * variables so do it before anything at all */
  if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
    quit(1, "Failed to curl_global_init");

#if LOCK_TRACKING
  // Must be first
  if (unlikely(pthread_mutex_init(&lockstat_lock, NULL)))
    quithere(1, "Failed to pthread_mutex_init lockstat_lock errno=%d", errno);
#endif

  // initialize default profile (globals) before reading config options
  init_default_profile();

  initial_args = (const char **)malloc(sizeof(char *)* (argc + 1));
  for  (i = 0; i < argc; i++)
    initial_args[i] = (const char *)strdup(argv[i]);
  initial_args[argc] = NULL;

  mutex_init(&eth_nonce_lock);
#ifdef WIN32
  rand_s(&eth_nonce);
  for (int i = 0; i < sizeof(entropy); i += 4)
    rand_s((uint32_t*)(entropy + i));
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    fd = open("/dev/random", O_RDONLY);
  read(fd, &eth_nonce, 4);
  read(fd, entropy, sizeof(entropy));
  close(fd);
#endif
  mutex_init(&hash_lock);
  mutex_init(&console_lock);
  cglock_init(&control_lock);
  mutex_init(&stats_lock);
  mutex_init(&sharelog_lock);
  cglock_init(&ch_lock);
  mutex_init(&sshare_lock);
  rwlock_init(&blk_lock);
  rwlock_init(&netacc_lock);
  rwlock_init(&mining_thr_lock);
  rwlock_init(&devices_lock);
  mutex_init(&algo_switch_lock);

  mutex_init(&lp_lock);
  if (unlikely(pthread_cond_init(&lp_cond, NULL)))
    quit(1, "Failed to pthread_cond_init lp_cond");

  mutex_init(&restart_lock);
  if (unlikely(pthread_cond_init(&restart_cond, NULL)))
    quit(1, "Failed to pthread_cond_init restart_cond");

  mutex_init(&algo_switch_wait_lock);
  if (unlikely(pthread_cond_init(&algo_switch_wait_cond, NULL)))
    quit(1, "Failed to pthread_cond_init algo_switch_wait_cond");

  if (unlikely(pthread_cond_init(&gws_cond, NULL)))
    quit(1, "Failed to pthread_cond_init gws_cond");

  /* Create a unique get work queue */
  getq = tq_new();
  if (!getq)
    quit(1, "Failed to create getq");
  /* We use the getq mutex as the staged lock */
  stgd_lock = &getq->mutex;

  snprintf(packagename, sizeof(packagename), "%s %s", PACKAGE, CGMINER_VERSION);

#ifndef WIN32
  signal(SIGPIPE, SIG_IGN);
#else
  timeBeginPeriod(1);
#endif

#ifndef _MSC_VER
  handler.sa_handler = &sighandler;
  handler.sa_flags = 0;
  sigemptyset(&handler.sa_mask);
  sigaction(SIGTERM, &handler, &termhandler);
  sigaction(SIGINT, &handler, &inthandler);
#endif

  /* enable debug output to file by default is stderr is redirected to a file */
  opt_debug = !isatty(fileno((FILE *)stderr));

  /* opt_kernel_path defaults to SGMINER_PREFIX */
  opt_kernel_path = (char *)alloca(PATH_MAX);
  strcpy(opt_kernel_path, SGMINER_PREFIX);

  /* sgminer_path is current dir */
  sgminer_path = (char *)alloca(PATH_MAX);
#ifndef _MSC_VER
  s = strdup(argv[0]);
  strcpy(sgminer_path, dirname(s));
  free(s);
#else
  GetCurrentDirectory(PATH_MAX - 1, sgminer_path);
#endif

  /* Default algorithm specified in algorithm.c ATM */
  set_algorithm(&default_profile.algorithm, "scrypt");

  devcursor = 8;
  logstart = devcursor + 1;
  logcursor = logstart + 1;

  block = (struct block *)calloc(sizeof(struct block), 1);
  if (unlikely(!block))
    quit (1, "main OOM");
  for (i = 0; i < 36; i++)
    strcat(block->hash, "0");
  HASH_ADD_STR(blocks, hash, block);
  strcpy(current_hash, block->hash);

  INIT_LIST_HEAD(&scan_devices);

  memset(gpus, 0, sizeof(gpus));
  for (i = 0; i < MAX_GPUDEVICES; i++)
    gpus[i].dynamic = true;

  /* parse config and command line */
  opt_register_table(opt_config_table,
         "Options for both config file and command line");
  opt_register_table(opt_cmdline_table,
         "Options for command line only");

  opt_parse(&argc, argv, applog_and_exit);
  if (argc != 1)
    quit(1, "Unexpected extra commandline arguments");

  if (!config_loaded)
    load_default_config();

  //load default profile if specified in config
  load_default_profile();

#ifdef HAVE_CURSES
  if (opt_realquiet || opt_display_devs)
    use_curses = false;

  if (use_curses)
    enable_curses();
#endif

  applog(LOG_WARNING, "Started %s", packagename);
  applog(LOG_WARNING, "* using Jansson %s", JANSSON_VERSION);
  if (cnfbuf) {
    applog(LOG_NOTICE, "Loaded configuration file %s", cnfbuf);
    switch (fileconf_load) {
      case 0:
        applog(LOG_WARNING, "Fatal JSON error in configuration file.");
        applog(LOG_WARNING, "Configuration file could not be used.");
        break;
      case -1:
        applog(LOG_WARNING, "Error in configuration file, partially loaded.");
        if (use_curses)
          applog(LOG_WARNING, "Start sgminer with -T to see what failed to load.");
        break;
      default:
        break;
    }
    free(cnfbuf);
    cnfbuf = NULL;
  }

  if (want_per_device_stats)
    opt_verbose = true;

  total_control_threads = 8;
  control_thr = (struct thr_info *)calloc(total_control_threads, sizeof(*thr));
  if (!control_thr)
    quit(1, "Failed to calloc control_thr");

  gwsched_thr_id = 0;

  //Detect GPUs
  /* Use the DRIVER_PARSE_COMMANDS macro to fill all the device_drvs */
  DRIVER_PARSE_COMMANDS(DRIVER_FILL_DEVICE_DRV)

  // this will set total_devices
  opencl_drv.drv_detect();

  if (opt_display_devs) {
    applog(LOG_ERR, "Devices detected:");
    for (i = 0; i < total_devices; ++i) {
      struct cgpu_info *cgpu = devices[i];
      if (cgpu->name)
        applog(LOG_ERR, " %2d. %s %d: %s (driver: %s)", i, cgpu->drv->name, cgpu->device_id, cgpu->name, cgpu->drv->dname);
      else
        applog(LOG_ERR, " %2d. %s %d (driver: %s)", i, cgpu->drv->name, cgpu->device_id, cgpu->drv->dname);
    }
    quit(0, "%d devices listed", total_devices);
  }

  //apply default settings to GPUs
  apply_defaults();

  //apply pool-specific config from profiles
  apply_pool_profiles();

  most_devices = 0;
  mining_threads = 0;
  if (opt_devs_enabled) {
    for (i = 0; i < MAX_DEVICES; i++) {
      if (devices_enabled[i]) {
        if (i >= total_devices)
          quit (1, "Command line options set a device that doesn't exist");
        enable_device(i);
        mining_threads += devices[i]->threads;
        most_devices++;
      } else if (i < total_devices) {
        devices[i]->deven = DEV_DISABLED;
        if (!opt_removedisabled)
          mining_threads += devices[i]->threads;
      }
    }
  } else {
    for (i = 0; i < total_devices; ++i) {
      enable_device(i);
      mining_threads += devices[i]->threads;
    }
    most_devices = total_devices;
  }

#ifdef HAVE_CURSES
  adj_width(mining_threads, &dev_width);
#endif

  if (mining_threads == 0)
    quit(1, "All devices disabled, cannot mine!");

  load_temp_cutoffs();

  rd_lock(&devices_lock);
  for (i = 0; i < total_devices; ++i)
    devices[i]->sgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
  rd_unlock(&devices_lock);

  if (!opt_compact) {
    logstart += (opt_removedisabled ? most_devices : total_devices);
    logcursor = logstart + 1;
#ifdef HAVE_CURSES
    check_winsizes();
#endif
  }

  if (!getenv("GPU_MAX_ALLOC_PERCENT"))
    applog(LOG_WARNING, "WARNING: GPU_MAX_ALLOC_PERCENT is not specified!");
  if (!getenv("GPU_USE_SYNC_OBJECTS"))
    applog(LOG_WARNING, "WARNING: GPU_USE_SYNC_OBJECTS is not specified!");

  if (!total_pools) {
    applog(LOG_WARNING, "Need to specify at least one pool server.");
#ifdef HAVE_CURSES
    if (!use_curses || !input_pool(false))
#endif
      quit(1, "Pool setup failed");
  }

  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];
    size_t siz;

    pool->sgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
    pool->sgminer_pool_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

    if (!pool->rpc_userpass) {
      if (!pool->rpc_user || !pool->rpc_pass)
        quit(1, "No login credentials supplied for %s", get_pool_name(pool));
      siz = strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2;
      pool->rpc_userpass = (char *)malloc(siz);
      if (!pool->rpc_userpass)
        quit(1, "Failed to malloc userpass");
      snprintf(pool->rpc_userpass, siz, "%s:%s", pool->rpc_user, pool->rpc_pass);
    }
  }
  /* Set the currentpool to pool 0 */
  set_current_pool(pools[0]);

#ifdef HAVE_SYSLOG_H
  if (use_syslog)
    openlog(PACKAGE, LOG_PID, LOG_USER);
#endif

  #if defined(unix) || defined(__APPLE__)
    if (opt_stderr_cmd)
      fork_monitor();
  #endif // defined(unix)

  /* Set pool state */
  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];

    switch (pool->state) {
    case POOL_DISABLED:
      disable_pool(pool);
      break;
    case POOL_ENABLED:
      enable_pool(pool);
      break;
    case POOL_HIDDEN:
      i--; /* Reiterate over this index. */
      remove_pool(pool);
      break;
    case POOL_REJECTING:
      reject_pool(pool);
      break;
    default:
      enable_pool(pool);
      break;
    }

    pool->idle = true;
  }

  applog(LOG_NOTICE, "Probing for an alive pool");
  int slept = 0;
  do {

    /* Look for at least one active pool before starting */
    probe_pools();
    do {
      sleep(1);
      slept++;
    } while (!pools_active && slept < 60);

    if (!pools_active) {
      applog(LOG_ERR, "No servers were found that could be used to get work from.");
      applog(LOG_ERR, "Please check the details from the list below of the servers you have input");
      applog(LOG_ERR, "Most likely you have input the wrong URL, forgotten to add a port, or have not set up workers");
      for (i = 0; i < total_pools; i++) {
        struct pool *pool;

        pool = pools[i];
        applog(LOG_WARNING, "Pool: %d  URL: %s  User: %s  Password: %s",
               i, pool->rpc_url, pool->rpc_user, pool->rpc_pass);
      }
#ifdef HAVE_CURSES
      if (use_curses) {
        halfdelay(150);
        applog(LOG_ERR, "Press any key to exit, or sgminer will try again in 15s.");
        if (getch() != ERR)
          quit(0, "No servers could be used! Exiting.");
        cbreak();
      } else
#endif
        quit(0, "No servers could be used! Exiting.");
    }
  } while (!pools_active);

  //wait for GPUs to be initialized after first alive pool is found
  slept = 0;
  do {
    sleep(1);
    slept++;
  } while (!gpu_initialized && slept < 60);

  if(slept >= 60)
    applog(LOG_WARNING, "GPUs did not become initialized in 60 seconds...");

  rd_lock(&devices_lock);
  total_mhashes_done = 0;
  for (i = 0; i < total_devices; i++) {
    struct cgpu_info *cgpu = devices[i];

    cgpu->rolling = cgpu->total_mhashes = 0;
  }
  rd_unlock(&devices_lock);

  cgtime(&total_tv_start);
  cgtime(&total_tv_end);
  get_datestamp(datestamp, sizeof(datestamp), &total_tv_start);
  launch_time = total_tv_start;

  watchpool_thr_id = 2;
  thr = &control_thr[watchpool_thr_id];
  /* start watchpool thread */
  if (thr_info_create(thr, NULL, watchpool_thread, NULL))
    quit(1, "watchpool thread create failed");
  pthread_detach(thr->pth);

  watchdog_thr_id = 3;
  thr = &control_thr[watchdog_thr_id];
  /* start watchdog thread */
  if (thr_info_create(thr, NULL, watchdog_thread, NULL))
    quit(1, "watchdog thread create failed");
  pthread_detach(thr->pth);

  /* Create reinit gpu thread */
  gpur_thr_id = 4;
  thr = &control_thr[gpur_thr_id];
  thr->q = tq_new();
  if (!thr->q)
    quit(1, "tq_new failed for gpur_thr_id");
  if (thr_info_create(thr, NULL, reinit_gpu, thr))
    quit(1, "reinit_gpu thread create failed");

  /* Create API socket thread */
  api_thr_id = 5;
  thr = &control_thr[api_thr_id];
  if (thr_info_create(thr, NULL, api_thread, thr))
    quit(1, "API thread create failed");

#ifdef HAVE_CURSES
  /* Create curses input thread for keyboard input. Create this last so
   * that we know all threads are created since this can call kill_work
   * to try and shut down all previous threads. */
  input_thr_id = 7;
  thr = &control_thr[input_thr_id];
  if (thr_info_create(thr, NULL, input_thread, thr))
    quit(1, "input thread create failed");
  pthread_detach(thr->pth);
#endif

  /* Just to be sure */
  if (total_control_threads != 8)
    quit(1, "incorrect total_control_threads (%d) should be 8", total_control_threads);

  /* Once everything is set up, main() becomes the getwork scheduler */
  while (42) {
    int ts, max_staged = opt_queue;
    struct pool *pool, *cp;
    bool lagging = false;
    struct timespec then;
    struct timeval now;
    struct work *work;

    if (opt_work_update)
      signal_work_update();
    opt_work_update = false;
    cp = current_pool();

    /* If the primary pool is a getwork pool and cannot roll work,
     * try to stage one extra work per mining thread */
    if (!pool_localgen(cp) && !staged_rollable)
      max_staged += mining_threads;

    cgtime(&now);
    then.tv_sec = now.tv_sec + 2;
    then.tv_nsec = now.tv_usec * 1000;

    mutex_lock(stgd_lock);
    ts = __total_staged();

    if (!pool_localgen(cp) && !ts && !opt_fail_only)
      lagging = true;

    /* Wait until hash_pop tells us we need to create more work */
    if (ts > max_staged) {
      pthread_cond_timedwait(&gws_cond, stgd_lock, &then);
      ts = __total_staged();
    }
    mutex_unlock(stgd_lock);

    if (ts > max_staged) {
      /* Keeps slowly generating work even if it's not being
       * used to keep last_getwork incrementing and to see
       * if pools are still alive. */
      work = hash_pop(false);
      if (work) {
        applog(LOG_DEBUG,
         "[THR%d] Staged work: total (%d) > max (%d), discarding",
         work->thr_id, ts, max_staged);
        discard_work(work);
      }
      continue;
    }

    work = make_work();

    if (lagging && !pool_tset(cp, &cp->lagging)) {
      applog(LOG_WARNING, "%s not providing work fast enough", cp->name);
      cp->getfail_occasions++;
      total_go++;
      if (!pool_localgen(cp))
        applog(LOG_INFO, "Increasing queue to %d", ++opt_queue);
    }
    pool = select_pool(lagging);
retry:
    if (pool->has_stratum) {
      while (!pool->stratum_active || !pool->stratum_notify) {
        struct pool *altpool = select_pool(true);

        cgsleep_ms(5000);
        if (altpool != pool) {
          pool = altpool;
          goto retry;
        }
      }
      if(pool->algorithm.type == ALGO_ETHASH) gen_stratum_work_eth(pool, work);
      else gen_stratum_work(pool, work);
      applog(LOG_DEBUG, "Generated stratum work");
      stage_work(work);
      continue;
    }

#ifdef HAVE_LIBCURL
    struct curl_ent *ce;

    if (pool->has_gbt) {
      while (pool->idle) {
        struct pool *altpool = select_pool(true);

        cgsleep_ms(5000);
        if (altpool != pool) {
          pool = altpool;
          goto retry;
        }
      }
      gen_gbt_work(pool, work);
      applog(LOG_DEBUG, "Generated GBT work");
      stage_work(work);
      continue;
    }

    if (clone_available()) {
      applog(LOG_DEBUG, "Cloned getwork work");
      free_work(work);
      continue;
    }

    work->pool = pool;
    ce = pop_curl_entry(pool);
    /* obtain new work from bitcoin via JSON-RPC */
    if (!get_upstream_work(work, ce->curl, ce->curl_err_str)) {
      applog(LOG_DEBUG, "%s json_rpc_call failed on get work, retrying in 5s", get_pool_name(pool));
      /* Make sure the pool just hasn't stopped serving
       * requests but is up as we'll keep hammering it */
      if (++pool->seq_getfails > mining_threads + opt_queue)
        pool_died(pool);
      cgsleep_ms(5000);
      push_curl_entry(ce, pool);
      pool = select_pool(!opt_fail_only);
      goto retry;
    }
    if (ts >= max_staged)
      pool_tclear(pool, &pool->lagging);
    if (pool_tclear(pool, &pool->idle))
      pool_resus(pool);

    applog(LOG_DEBUG, "Generated getwork work");
    stage_work(work);
    push_curl_entry(ce, pool);
#endif /* HAVE_LIBCURL */
  }

  return 0;
}
