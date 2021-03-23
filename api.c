/*
 * Copyright 2011-2014 Andrew Smith
 * Copyright 2011-2013 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 * Note: the code always includes GPU support even if there are no GPUs
 *  this simplifies handling multiple other device code being included
 *  depending on compile options
 */
#define _MEMORY_DEBUG_MASTER 1

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>

#include "compat.h"
#include "api.h"
#include "miner.h"
#include "pool.h"
#include "util.h"
#include "pool.h"
#include "algorithm.h"

#include "config_parser.h"
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define SIG_CANCEL_SIGNAL SIGUSR1
#define PTHREAD_CANCEL_ENABLE 1
#define PTHREAD_CANCEL_DISABLE 0

typedef long pthread_t;

static int pthread_setcancelstate(int state, int *oldstate) {
    sigset_t   new, old;
    int ret;
    sigemptyset (&new);
    sigaddset (&new, SIG_CANCEL_SIGNAL);

    ret = pthread_sigmask(state == PTHREAD_CANCEL_ENABLE ? SIG_BLOCK : SIG_UNBLOCK, &new , &old);
    if(oldstate != NULL)
    {
        *oldstate =sigismember(&old,SIG_CANCEL_SIGNAL) == 0 ? PTHREAD_CANCEL_DISABLE : PTHREAD_CANCEL_ENABLE;
    }
    return ret;
}

static inline int pthread_cancel(pthread_t thread) {
    return pthread_kill(thread, SIG_CANCEL_SIGNAL);
}
#ifdef WIN32
static char WSAbuf[1024];

struct WSAERRORS {
  int id;
  char *code;
} WSAErrors[] = {
  { 0,      "No error" },
  { WSAEINTR,   "Interrupted system call" },
  { WSAEBADF,   "Bad file number" },
  { WSAEACCES,    "Permission denied" },
  { WSAEFAULT,    "Bad address" },
  { WSAEINVAL,    "Invalid argument" },
  { WSAEMFILE,    "Too many open sockets" },
  { WSAEWOULDBLOCK, "Operation would block" },
  { WSAEINPROGRESS, "Operation now in progress" },
  { WSAEALREADY,    "Operation already in progress" },
  { WSAENOTSOCK,    "Socket operation on non-socket" },
  { WSAEDESTADDRREQ,  "Destination address required" },
  { WSAEMSGSIZE,    "Message too long" },
  { WSAEPROTOTYPE,  "Protocol wrong type for socket" },
  { WSAENOPROTOOPT, "Bad protocol option" },
  { WSAEPROTONOSUPPORT, "Protocol not supported" },
  { WSAESOCKTNOSUPPORT, "Socket type not supported" },
  { WSAEOPNOTSUPP,  "Operation not supported on socket" },
  { WSAEPFNOSUPPORT,  "Protocol family not supported" },
  { WSAEAFNOSUPPORT,  "Address family not supported" },
  { WSAEADDRINUSE,  "Address already in use" },
  { WSAEADDRNOTAVAIL, "Can't assign requested address" },
  { WSAENETDOWN,    "Network is down" },
  { WSAENETUNREACH, "Network is unreachable" },
  { WSAENETRESET,   "Net connection reset" },
  { WSAECONNABORTED,  "Software caused connection abort" },
  { WSAECONNRESET,  "Connection reset by peer" },
  { WSAENOBUFS,   "No buffer space available" },
  { WSAEISCONN,   "Socket is already connected" },
  { WSAENOTCONN,    "Socket is not connected" },
  { WSAESHUTDOWN,   "Can't send after socket shutdown" },
  { WSAETOOMANYREFS,  "Too many references, can't splice" },
  { WSAETIMEDOUT,   "Connection timed out" },
  { WSAECONNREFUSED,  "Connection refused" },
  { WSAELOOP,   "Too many levels of symbolic links" },
  { WSAENAMETOOLONG,  "File name too long" },
  { WSAEHOSTDOWN,   "Host is down" },
  { WSAEHOSTUNREACH,  "No route to host" },
  { WSAENOTEMPTY,   "Directory not empty" },
  { WSAEPROCLIM,    "Too many processes" },
  { WSAEUSERS,    "Too many users" },
  { WSAEDQUOT,    "Disc quota exceeded" },
  { WSAESTALE,    "Stale NFS file handle" },
  { WSAEREMOTE,   "Too many levels of remote in path" },
  { WSASYSNOTREADY, "Network system is unavailable" },
  { WSAVERNOTSUPPORTED, "Winsock version out of range" },
  { WSANOTINITIALISED,  "WSAStartup not yet called" },
  { WSAEDISCON,   "Graceful shutdown in progress" },
  { WSAHOST_NOT_FOUND,  "Host not found" },
  { WSANO_DATA,   "No host data of that type was found" },
  { -1,     "Unknown error code" }
};

char *WSAErrorMsg(void) {
  int i;
  int id = WSAGetLastError();

  /* Assume none of them are actually -1 */
  for (i = 0; WSAErrors[i].id != -1; i++)
    if (WSAErrors[i].id == id)
      break;

  sprintf(WSAbuf, "Socket Error: (%d) %s", id, WSAErrors[i].code);

  return &(WSAbuf[0]);
}
#endif


struct CODES codes[] = {
 { SEVERITY_ERR,   MSG_INVGPU,  PARAM_GPUMAX, "Invalid GPU id %d - range is 0 - %d" },
 { SEVERITY_INFO,  MSG_ALRENA,  PARAM_GPU,  "GPU %d already enabled" },
 { SEVERITY_INFO,  MSG_ALRDIS,  PARAM_GPU,  "GPU %d already disabled" },
 { SEVERITY_WARN,  MSG_GPUMRE,  PARAM_GPU,  "GPU %d must be restarted first" },
 { SEVERITY_INFO,  MSG_GPUREN,  PARAM_GPU,  "GPU %d sent enable message" },
 { SEVERITY_ERR,   MSG_GPUNON,  PARAM_NONE, "No GPUs" },
 { SEVERITY_SUCC,  MSG_POOL,  PARAM_PMAX, "%d Pool(s)" },
 { SEVERITY_ERR,   MSG_NOPOOL,  PARAM_NONE, "No pools" },

 { SEVERITY_SUCC,  MSG_DEVS,  PARAM_DMAX,   "%d GPU(s)" },
 { SEVERITY_ERR,   MSG_NODEVS,  PARAM_NONE, "No GPUs"
 },

 { SEVERITY_SUCC,  MSG_SUMM,  PARAM_NONE, "Summary" },
 { SEVERITY_INFO,  MSG_GPUDIS,  PARAM_GPU,  "GPU %d set disable flag" },
 { SEVERITY_INFO,  MSG_GPUREI,  PARAM_GPU,  "GPU %d restart attempted" },
 { SEVERITY_ERR,   MSG_INVCMD,  PARAM_NONE, "Invalid command" },
 { SEVERITY_ERR,   MSG_MISID, PARAM_NONE, "Missing device id parameter" },
 { SEVERITY_SUCC,  MSG_GPUDEV,  PARAM_GPU,  "GPU%d" },
 { SEVERITY_SUCC,  MSG_NUMGPU,  PARAM_NONE, "GPU count" },
 { SEVERITY_SUCC,  MSG_VERSION, PARAM_NONE, "SGMiner versions" },
 { SEVERITY_ERR,   MSG_INVJSON, PARAM_NONE, "Invalid JSON" },
 { SEVERITY_ERR,   MSG_MISCMD,  PARAM_CMD,  "Missing JSON '%s'" },
 { SEVERITY_ERR,   MSG_MISPID,  PARAM_NONE, "Missing pool id parameter" },
 { SEVERITY_ERR,   MSG_INVPID,  PARAM_POOLMAX,  "Invalid pool id %d - range is 0 - %d" },
 { SEVERITY_SUCC,  MSG_SWITCHP, PARAM_POOL, "Switching to pool %d:'%s'" },
 { SEVERITY_ERR,   MSG_MISVAL,  PARAM_NONE, "Missing comma after GPU number" },
 { SEVERITY_ERR,   MSG_NOADL, PARAM_NONE, "ADL is not available" },
 { SEVERITY_ERR,   MSG_NOGPUADL,PARAM_GPU,  "GPU %d does not have ADL" },
 { SEVERITY_ERR,   MSG_INVINT,  PARAM_STR,  "Invalid intensity (%s) - must be '" _DYNAMIC  "' or range " MIN_INTENSITY_STR " - " MAX_INTENSITY_STR },
 { SEVERITY_INFO,  MSG_GPUINT,  PARAM_BOTH, "GPU %d set new intensity to %s" },
 { SEVERITY_ERR,   MSG_INVXINT,  PARAM_STR,  "Invalid xintensity (%s) - must be range " MIN_XINTENSITY_STR " - " MAX_XINTENSITY_STR },
 { SEVERITY_INFO,  MSG_GPUXINT,  PARAM_BOTH, "GPU %d set new xintensity to %s" },
 { SEVERITY_ERR,   MSG_INVRAWINT,  PARAM_STR,  "Invalid rawintensity (%s) - must be range " MIN_RAWINTENSITY_STR " - " MAX_RAWINTENSITY_STR },
 { SEVERITY_INFO,  MSG_GPURAWINT,  PARAM_BOTH, "GPU %d set new rawintensity to %s" },
 { SEVERITY_SUCC,  MSG_MINECONFIG,PARAM_NONE, "sgminer config" },
 { SEVERITY_ERR,   MSG_GPUMERR, PARAM_BOTH, "Setting GPU %d memoryclock to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUMEM,  PARAM_BOTH, "Setting GPU %d memoryclock to (%s) reported success" },
 { SEVERITY_ERR,   MSG_GPUEERR, PARAM_BOTH, "Setting GPU %d clock to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUENG,  PARAM_BOTH, "Setting GPU %d clock to (%s) reported success" },
 { SEVERITY_ERR,   MSG_GPUVERR, PARAM_BOTH, "Setting GPU %d vddc to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUVDDC, PARAM_BOTH, "Setting GPU %d vddc to (%s) reported success" },
 { SEVERITY_ERR,   MSG_GPUFERR, PARAM_BOTH, "Setting GPU %d fan to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUFAN,  PARAM_BOTH, "Setting GPU %d fan to (%s) reported success" },
 { SEVERITY_ERR,   MSG_MISFN, PARAM_NONE, "Missing save filename parameter" },
 { SEVERITY_ERR,   MSG_BADFN, PARAM_STR,  "Can't open or create save file '%s'" },
 { SEVERITY_SUCC,  MSG_SAVED, PARAM_STR,  "Configuration saved to file '%s'" },
 { SEVERITY_ERR,   MSG_ACCDENY, PARAM_STR,  "Access denied to '%s' command" },
 { SEVERITY_SUCC,  MSG_ACCOK, PARAM_NONE, "Privileged access OK" },
 { SEVERITY_SUCC,  MSG_ENAPOOL, PARAM_POOL, "Enabling pool %d:'%s'" },
 { SEVERITY_SUCC,  MSG_POOLPRIO,PARAM_NONE, "Changed pool priorities" },
 { SEVERITY_ERR,   MSG_DUPPID,  PARAM_PID,  "Duplicate pool specified %d" },
 { SEVERITY_SUCC,  MSG_DISPOOL, PARAM_POOL, "Disabling pool %d:'%s'" },
 { SEVERITY_INFO,  MSG_ALRENAP, PARAM_POOL, "Pool %d:'%s' already enabled" },
 { SEVERITY_INFO,  MSG_ALRDISP, PARAM_POOL, "Pool %d:'%s' already disabled" },
 { SEVERITY_ERR,   MSG_MISPDP,  PARAM_NONE, "Missing addpool details" },
 { SEVERITY_ERR,   MSG_INVPDP,  PARAM_STR,  "Invalid addpool details '%s'" },
 { SEVERITY_ERR,   MSG_TOOMANYP,PARAM_NONE, "Reached maximum number of pools (%d)" },
 { SEVERITY_SUCC,  MSG_ADDPOOL, PARAM_STR,  "Added pool '%s'" },
 { SEVERITY_ERR,   MSG_REMLASTP,PARAM_POOL, "Cannot remove last pool %d:'%s'" },
 { SEVERITY_ERR,   MSG_ACTPOOL, PARAM_POOL, "Cannot remove active pool %d:'%s'" },
 { SEVERITY_SUCC,  MSG_REMPOOL, PARAM_BOTH, "Removed pool %d:'%s'" },
 { SEVERITY_SUCC,  MSG_NOTIFY,  PARAM_NONE, "Notify" },
 { SEVERITY_SUCC,  MSG_DEVDETAILS,PARAM_NONE, "Device Details" },
 { SEVERITY_SUCC,  MSG_MINESTATS,PARAM_NONE,  "sgminer stats" },
 { SEVERITY_ERR,   MSG_MISCHK,  PARAM_NONE, "Missing check cmd" },
 { SEVERITY_SUCC,  MSG_CHECK, PARAM_NONE, "Check command" },
 { SEVERITY_ERR,   MSG_MISBOOL, PARAM_NONE, "Missing parameter: true/false" },
 { SEVERITY_ERR,   MSG_INVBOOL, PARAM_NONE, "Invalid parameter should be true or false" },
 { SEVERITY_SUCC,  MSG_FOO, PARAM_BOOL, "Failover-Only set to %s" },
 { SEVERITY_SUCC,  MSG_MINECOIN,PARAM_NONE, "sgminer coin" },
 { SEVERITY_SUCC,  MSG_DEBUGSET,PARAM_NONE, "Debug settings" },
 { SEVERITY_SUCC,  MSG_SETCONFIG,PARAM_SET, "Set config '%s' to %d" },
 { SEVERITY_ERR,   MSG_UNKCON,  PARAM_STR,  "Unknown config '%s'" },
 { SEVERITY_ERR,   MSG_INVNUM,  PARAM_BOTH, "Invalid number (%d) for '%s' range is 0-9999" },
 { SEVERITY_ERR,   MSG_INVNEG,  PARAM_BOTH, "Invalid negative number (%d) for '%s'" },
 { SEVERITY_SUCC,  MSG_SETQUOTA,PARAM_SET,  "Set pool '%s' to quota %d'" },
 { SEVERITY_ERR,   MSG_CONPAR,  PARAM_NONE, "Missing config parameters 'name,N'" },
 { SEVERITY_ERR,   MSG_CONVAL,  PARAM_STR,  "Missing config value N for '%s,N'" },
 { SEVERITY_INFO,  MSG_NOUSTA,  PARAM_NONE, "No USB Statistics" },
 { SEVERITY_ERR,   MSG_ZERMIS,  PARAM_NONE, "Missing zero parameters" },
 { SEVERITY_ERR,   MSG_ZERINV,  PARAM_STR,  "Invalid zero parameter '%s'" },
 { SEVERITY_SUCC,  MSG_ZERSUM,  PARAM_STR,  "Zeroed %s stats with summary" },
 { SEVERITY_SUCC,  MSG_ZERNOSUM, PARAM_STR, "Zeroed %s stats without summary" },
 { SEVERITY_SUCC,  MSG_LOCKOK,  PARAM_NONE, "Lock stats created" },
 { SEVERITY_WARN,  MSG_LOCKDIS, PARAM_NONE, "Lock stats not enabled" },
 { SEVERITY_SUCC,  MSG_CHSTRAT,  PARAM_STR, "Multipool strategy changed to '%s'" },
 { SEVERITY_ERR,   MSG_MISSTRAT, PARAM_NONE, "Missing multipool strategy" },
 { SEVERITY_ERR,   MSG_INVSTRAT, PARAM_NONE, "Invalid multipool strategy %d" },
 { SEVERITY_ERR,   MSG_MISSTRATINT, PARAM_NONE, "Missing rotate interval" },

 { SEVERITY_SUCC,  MSG_PROFILE,  PARAM_PRMAX, "%d Profile(s)" },
 { SEVERITY_ERR,   MSG_NOPROFILE,  PARAM_NONE, "No profiles" },

 { SEVERITY_ERR,   MSG_PROFILEEXIST,  PARAM_STR, "Profile '%s' already exists" },
 { SEVERITY_ERR,   MSG_MISPRD,  PARAM_NONE, "Missing addprofile details" },
 { SEVERITY_SUCC,  MSG_ADDPROFILE, PARAM_STR,  "Added profile '%s'" },

 { SEVERITY_ERR,   MSG_MISPRID,  PARAM_STR, "Profile name missing" },
 { SEVERITY_ERR,   MSG_PRNOEXIST,  PARAM_STR, "Profile '%s' doesn't exist" },
 { SEVERITY_ERR,   MSG_PRISDEFAULT,  PARAM_STR, "Profile '%s' is the default profile" },
 { SEVERITY_ERR,   MSG_PRINUSE,  PARAM_STR, "Profile '%s' is used by a pool" },
 { SEVERITY_SUCC,  MSG_REMPROFILE, PARAM_BOTH, "Removed pool %d:'%s'" },

 { SEVERITY_SUCC,  MSG_CHPOOLPR, PARAM_BOTH, "Changed pool %d to profile '%s'" },

 { SEVERITY_SUCC,  MSG_BYE,   PARAM_STR,  "%s" },
 { SEVERITY_FAIL, 0, (enum code_parameters)0, NULL }
};


static const char *UNAVAILABLE = " - API will not be available";
static const char *MUNAVAILABLE = " - API multicast listener will not be available";

static const char *BLANK = "";
static const char *COMMA = ",";
static const char SEPARATOR = '|';
static const char GPUSEP = ',';
static const char *APIVERSION = "4.0";
static const char *DEAD = "Dead";
static const char *SICK = "Sick";
static const char *NOSTART = "NoStart";
static const char *INIT = "Initialising";
static const char *DISABLED = "Disabled";
static const char *ALIVE = "Alive";
static const char *REJECTING = "Rejecting";
static const char *UNKNOWN = "Unknown";
static const char *DYNAMIC = _DYNAMIC;

static __maybe_unused const char *NONE = "None";

static const char *YES = "Y";
static const char *NO = "N";
static const char *NULLSTR = "(null)";

static const char *TRUESTR = "true";
static const char *FALSESTR = "false";

static const char *DEVICECODE = "GPU ";

static const char *OSINFO =
#if defined(__linux__)
      "Linux";
#elif defined(__APPLE__)
      "Apple";
#elif defined(WIN32)
      "Windows";
#elif defined(__CYGWIN__)
      "Cygwin";
#elif defined(__unix__)
      "Unix";
#else
      "Unknown";
#endif

static const char *JSON_COMMAND = "command";
static const char *JSON_PARAMETER = "parameter";
static const char ISJSON = '{';
static const char *localaddr = "127.0.0.1";

static int my_thr_id = 0;
static bool bye;

// Used to control quit restart access to shutdown variables
static pthread_mutex_t quit_restart_lock;

static bool do_a_quit;
static bool do_a_restart;

static time_t when = 0; // when the request occurred

static struct IP4ACCESS *ipaccess = NULL;
static int ips = 0;

struct APIGROUPS {
  // This becomes a string like: "|cmd1|cmd2|cmd3|" so it's quick to search
  char *commands;
} apigroups['Z' - 'A' + 1]; // only A=0 to Z=25 (R: noprivs, W: allprivs)

static struct io_list *io_head = NULL;

static void io_reinit(struct io_data *io_data)
{
  io_data->cur = io_data->ptr;
  *(io_data->ptr) = '\0';
  io_data->close = false;
}

static struct io_data *_io_new(size_t initial, bool socket_buf)
{
  struct io_data *io_data;
  struct io_list *io_list;

  io_data = (struct io_data *)malloc(sizeof(*io_data));
  io_data->ptr = (char *)malloc(initial);
  io_data->siz = initial;
  io_data->sock = socket_buf;
  io_reinit(io_data);

  io_list = (struct io_list *)malloc(sizeof(*io_list));

  io_list->io_data = io_data;

  if (io_head) {
    io_list->next = io_head;
    io_list->prev = io_head->prev;
    io_list->next->prev = io_list;
    io_list->prev->next = io_list;
  } else {
    io_list->prev = io_list;
    io_list->next = io_list;
    io_head = io_list;
  }

  return io_data;
}

bool io_add(struct io_data *io_data, char *buf)
{
  size_t len, dif, tot;

  len = strlen(buf);
  dif = io_data->cur - io_data->ptr;
  // send will always have enough space to add the JSON
  tot = len + 1 + dif + sizeof(JSON_CLOSE) + sizeof(JSON_END);

  if (tot > io_data->siz) {
    size_t newsize = io_data->siz + (2 * SOCKBUFALLOCSIZ);

    if (newsize < tot)
      newsize = (2 + (size_t)((float)tot / (float)SOCKBUFALLOCSIZ)) * SOCKBUFALLOCSIZ;

    io_data->ptr = (char *)realloc(io_data->ptr, newsize);
    io_data->cur = io_data->ptr + dif;
    io_data->siz = newsize;
  }

  memcpy(io_data->cur, buf, len + 1);
  io_data->cur += len;

  return true;
}

void io_close(struct io_data *io_data)
{
  io_data->close = true;
}

void io_free()
{
  struct io_list *io_list, *io_next;

  if (io_head) {
    io_list = io_head;
    do {
      io_next = io_list->next;

      free(io_list->io_data->ptr);
      free(io_list->io_data);
      free(io_list);

      io_list = io_next;
    } while (io_list != io_head);

    io_head = NULL;
  }
}

// This is only called when expected to be needed (rarely)
// i.e. strings outside of the codes control (input from the user)
static char *escape_string(char *str, bool isjson)
{
  char *buf, *ptr;
  int count;

  count = 0;
  for (ptr = str; *ptr; ptr++) {
    switch (*ptr) {
      case ',':
      case '|':
      case '=':
        if (!isjson)
          count++;
        break;
      case '"':
        if (isjson)
          count++;
        break;
      case '\\':
        count++;
        break;
    }
  }

  if (count == 0)
    return str;

  buf = (char *)malloc(strlen(str) + count + 1);
  if (unlikely(!buf))
    quit(1, "Failed to malloc escape buf");

  ptr = buf;
  while (*str)
    switch (*str) {
      case ',':
      case '|':
      case '=':
        if (!isjson)
          *(ptr++) = '\\';
        *(ptr++) = *(str++);
        break;
      case '"':
        if (isjson)
          *(ptr++) = '\\';
        *(ptr++) = *(str++);
        break;
      case '\\':
        *(ptr++) = '\\';
        *(ptr++) = *(str++);
        break;
      default:
        *(ptr++) = *(str++);
        break;
    }

  *ptr = '\0';

  return buf;
}

static struct api_data *api_add_extra(struct api_data *root, struct api_data *extra)
{
  struct api_data *tmp;

  if (root) {
    if (extra) {
      // extra tail
      tmp = extra->prev;

      // extra prev = root tail
      extra->prev = root->prev;

      // root tail next = extra
      root->prev->next = extra;

      // extra tail next = root
      tmp->next = root;

      // root prev = extra tail
      root->prev = tmp;
    }
  } else
    root = extra;

  return root;
}

static struct api_data *api_add_data_full(struct api_data *root, char *name, enum api_data_type type, void *data, bool copy_data)
{
  struct api_data *api_data;

  api_data = (struct api_data *)malloc(sizeof(struct api_data));

  api_data->name = strdup(name);
  api_data->type = type;

  if (root == NULL) {
    root = api_data;
    root->prev = root;
    root->next = root;
  } else {
    api_data->prev = root->prev;
    root->prev = api_data;
    api_data->next = root;
    api_data->prev->next = api_data;
  }

  api_data->data_was_malloc = copy_data;

  // Avoid crashing on bad data
  if (data == NULL) {
    api_data->type = type = API_CONST;
    data = (void *)NULLSTR;
    api_data->data_was_malloc = copy_data = false;
  }

  if (!copy_data)
    api_data->data = data;
  else
    switch(type) {
      case API_ESCAPE:
      case API_STRING:
      case API_CONST:
        api_data->data = (void *)malloc(strlen((char *)data) + 1);
        strcpy((char*)(api_data->data), (char *)data);
        break;
      case API_UINT8:
        /* Most OSs won't really alloc less than 4 */
        api_data->data = malloc(4);
        *(uint8_t *)api_data->data = *(uint8_t *)data;
        break;
      case API_UINT16:
        /* Most OSs won't really alloc less than 4 */
        api_data->data = malloc(4);
        *(uint16_t *)api_data->data = *(uint16_t *)data;
        break;
      case API_INT:
        api_data->data = (void *)malloc(sizeof(int));
        *((int *)(api_data->data)) = *((int *)data);
        break;
      case API_UINT:
        api_data->data = (void *)malloc(sizeof(unsigned int));
        *((unsigned int *)(api_data->data)) = *((unsigned int *)data);
        break;
      case API_UINT32:
        api_data->data = (void *)malloc(sizeof(uint32_t));
        *((uint32_t *)(api_data->data)) = *((uint32_t *)data);
        break;
      case API_HEX32:
        api_data->data = (void *)malloc(sizeof(uint32_t));
        *((uint32_t *)(api_data->data)) = *((uint32_t *)data);
        break;
      case API_UINT64:
        api_data->data = (void *)malloc(sizeof(uint64_t));
        *((uint64_t *)(api_data->data)) = *((uint64_t *)data);
        break;
      case API_DOUBLE:
      case API_ELAPSED:
      case API_MHS:
      case API_KHS:
      case API_MHTOTAL:
      case API_UTILITY:
      case API_FREQ:
      case API_HS:
      case API_DIFF:
      case API_PERCENT:
        api_data->data = (void *)malloc(sizeof(double));
        *((double *)(api_data->data)) = *((double *)data);
        break;
      case API_BOOL:
        api_data->data = (void *)malloc(sizeof(bool));
        *((bool *)(api_data->data)) = *((bool *)data);
        break;
      case API_TIMEVAL:
        api_data->data = (void *)malloc(sizeof(struct timeval));
        memcpy(api_data->data, data, sizeof(struct timeval));
        break;
      case API_TIME:
        api_data->data = (void *)malloc(sizeof(time_t));
        *(time_t *)(api_data->data) = *((time_t *)data);
        break;
      case API_VOLTS:
      case API_TEMP:
      case API_AVG:
        api_data->data = (void *)malloc(sizeof(float));
        *((float *)(api_data->data)) = *((float *)data);
        break;
      default:
        applog(LOG_ERR, "API: unknown1 data type %d ignored", type);
        api_data->type = API_STRING;
        api_data->data_was_malloc = false;
        api_data->data = (void *)UNKNOWN;
        break;
    }

  return root;
}

struct api_data *api_add_escape(struct api_data *root, char *name, char *data, bool copy_data)
{
  return api_add_data_full(root, name, API_ESCAPE, (void *)data, copy_data);
}

struct api_data *api_add_string(struct api_data *root, char *name, char *data, bool copy_data)
{
  return api_add_data_full(root, name, API_STRING, (void *)data, copy_data);
}

struct api_data *api_add_const(struct api_data *root, char *name, const char *data, bool copy_data)
{
  return api_add_data_full(root, name, API_CONST, (void *)data, copy_data);
}

struct api_data *api_add_uint8(struct api_data *root, char *name, uint8_t *data, bool copy_data)
{
  return api_add_data_full(root, name, API_UINT8, (void *)data, copy_data);
}

struct api_data *api_add_uint16(struct api_data *root, char *name, uint16_t *data, bool copy_data)
{
  return api_add_data_full(root, name, API_UINT16, (void *)data, copy_data);
}

struct api_data *api_add_int(struct api_data *root, char *name, int *data, bool copy_data)
{
  return api_add_data_full(root, name, API_INT, (void *)data, copy_data);
}

struct api_data *api_add_uint(struct api_data *root, char *name, unsigned int *data, bool copy_data)
{
  return api_add_data_full(root, name, API_UINT, (void *)data, copy_data);
}

struct api_data *api_add_uint32(struct api_data *root, char *name, uint32_t *data, bool copy_data)
{
  return api_add_data_full(root, name, API_UINT32, (void *)data, copy_data);
}

struct api_data *api_add_hex32(struct api_data *root, char *name, uint32_t *data, bool copy_data)
{
  return api_add_data_full(root, name, API_HEX32, (void *)data, copy_data);
}

struct api_data *api_add_uint64(struct api_data *root, char *name, uint64_t *data, bool copy_data)
{
  return api_add_data_full(root, name, API_UINT64, (void *)data, copy_data);
}

struct api_data *api_add_double(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_DOUBLE, (void *)data, copy_data);
}

struct api_data *api_add_elapsed(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_ELAPSED, (void *)data, copy_data);
}

struct api_data *api_add_bool(struct api_data *root, char *name, bool *data, bool copy_data)
{
  return api_add_data_full(root, name, API_BOOL, (void *)data, copy_data);
}

struct api_data *api_add_timeval(struct api_data *root, char *name, struct timeval *data, bool copy_data)
{
  return api_add_data_full(root, name, API_TIMEVAL, (void *)data, copy_data);
}

struct api_data *api_add_time(struct api_data *root, char *name, time_t *data, bool copy_data)
{
  return api_add_data_full(root, name, API_TIME, (void *)data, copy_data);
}

struct api_data *api_add_mhs(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_MHS, (void *)data, copy_data);
}

struct api_data *api_add_khs(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_KHS, (void *)data, copy_data);
}

struct api_data *api_add_mhtotal(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_MHTOTAL, (void *)data, copy_data);
}

struct api_data *api_add_temp(struct api_data *root, char *name, float *data, bool copy_data)
{
  return api_add_data_full(root, name, API_TEMP, (void *)data, copy_data);
}

struct api_data *api_add_utility(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_UTILITY, (void *)data, copy_data);
}

struct api_data *api_add_freq(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_FREQ, (void *)data, copy_data);
}

struct api_data *api_add_volts(struct api_data *root, char *name, float *data, bool copy_data)
{
  return api_add_data_full(root, name, API_VOLTS, (void *)data, copy_data);
}

struct api_data *api_add_hs(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_HS, (void *)data, copy_data);
}

struct api_data *api_add_diff(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_DIFF, (void *)data, copy_data);
}

struct api_data *api_add_percent(struct api_data *root, char *name, double *data, bool copy_data)
{
  return api_add_data_full(root, name, API_PERCENT, (void *)data, copy_data);
}

struct api_data *api_add_avg(struct api_data *root, char *name, float *data, bool copy_data)
{
  return api_add_data_full(root, name, API_AVG, (void *)data, copy_data);
}

struct api_data *print_data(struct api_data *root, char *buf, bool isjson, bool precom)
{
  struct api_data *tmp;
  bool first = true;
  char *original, *escape;
  char *quote;

  *buf = '\0';

  if (precom) {
    *(buf++) = *COMMA;
    *buf = '\0';
  }

  if (isjson) {
    strcpy(buf, JSON0);
    buf = strchr(buf, '\0');
    quote = JSON1;
  } else
    quote = (char *)BLANK;

  while (root) {
    if (!first)
      *(buf++) = *COMMA;
    else
      first = false;

    sprintf(buf, "%s%s%s%s", quote, root->name, quote, isjson ? ":" : "=");

    buf = strchr(buf, '\0');

    switch(root->type) {
      case API_STRING:
      case API_CONST:
        sprintf(buf, "%s%s%s", quote, (char *)(root->data), quote);
        break;
      case API_ESCAPE:
        original = (char *)(root->data);
        escape = escape_string((char *)(root->data), isjson);
        sprintf(buf, "%s%s%s", quote, escape, quote);
        if (escape != original)
          free(escape);
        break;
      case API_UINT8:
        sprintf(buf, "%u", *(uint8_t *)root->data);
        break;
      case API_UINT16:
        sprintf(buf, "%u", *(uint16_t *)root->data);
        break;
      case API_INT:
        sprintf(buf, "%d", *((int *)(root->data)));
        break;
      case API_UINT:
        sprintf(buf, "%u", *((unsigned int *)(root->data)));
        break;
      case API_UINT32:
        sprintf(buf, "%"PRIu32, *((uint32_t *)(root->data)));
        break;
      case API_HEX32:
        snprintf(buf, strlen(buf) + 1, "0x%08x", *((uint32_t *)(root->data)));
        break;
      case API_UINT64:
        sprintf(buf, "%"PRIu64, *((uint64_t *)(root->data)));
        break;
      case API_TIME:
        sprintf(buf, "%lu", *((unsigned long *)(root->data)));
        break;
      case API_DOUBLE:
        sprintf(buf, "%f", *((double *)(root->data)));
        break;
      case API_ELAPSED:
        sprintf(buf, "%.0f", *((double *)(root->data)));
        break;
      case API_UTILITY:
      case API_FREQ:
      case API_MHS:
        sprintf(buf, "%.4f", *((double *)(root->data)));
        break;
      case API_KHS:
        sprintf(buf, "%.0f", *((double *)(root->data)));
        break;
      case API_VOLTS:
      case API_AVG:
        sprintf(buf, "%.3f", *((float *)(root->data)));
        break;
      case API_MHTOTAL:
        sprintf(buf, "%.4f", *((double *)(root->data)));
        break;
      case API_HS:
        sprintf(buf, "%.15f", *((double *)(root->data)));
        break;
      case API_DIFF:
        sprintf(buf, "%.8f", *((double *)(root->data)));
        break;
      case API_BOOL:
        sprintf(buf, "%s", *((bool *)(root->data)) ? TRUESTR : FALSESTR);
        break;
      case API_TIMEVAL:
        sprintf(buf, "%"PRIu64".%06lu",
          (uint64_t)((struct timeval *)(root->data))->tv_sec,
          (unsigned long)((struct timeval *)(root->data))->tv_usec);
        break;
      case API_TEMP:
        sprintf(buf, "%.2f", *((float *)(root->data)));
        break;
      case API_PERCENT:
        sprintf(buf, "%.4f", *((double *)(root->data)) * 100.0);
        break;
      default:
        applog(LOG_ERR, "API: unknown2 data type %d ignored", root->type);
        sprintf(buf, "%s%s%s", quote, UNKNOWN, quote);
        break;
    }

    buf = strchr(buf, '\0');

    free(root->name);
    if (root->data_was_malloc)
      free(root->data);

    if (root->next == root) {
      free(root);
      root = NULL;
    } else {
      tmp = root;
      root = tmp->next;
      root->prev = tmp->prev;
      root->prev->next = root;
      free(tmp);
    }
  }

  strcpy(buf, isjson ? JSON5 : SEPSTR);

  return root;
}

// All replies (except BYE and RESTART) start with a message
//  thus for JSON, message() inserts JSON_START at the front
//  and send_result() adds JSON_END at the end
void message(struct io_data *io_data, int messageid, int paramid, char *param2, bool isjson)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  char buf2[TMPBUFSIZ];
  char severity[2];

  int i;

  if (isjson)
    io_add(io_data, JSON_START JSON_STATUS);

  for (i = 0; codes[i].severity != SEVERITY_FAIL; i++) {
    if (codes[i].code == messageid) {
      switch (codes[i].severity) {
        case SEVERITY_WARN:
          severity[0] = 'W';
          break;
        case SEVERITY_INFO:
          severity[0] = 'I';
          break;
        case SEVERITY_SUCC:
          severity[0] = 'S';
          break;
        case SEVERITY_ERR:
        default:
          severity[0] = 'E';
          break;
      }
      severity[1] = '\0';

      switch(codes[i].params) {
        case PARAM_GPU:
        case PARAM_PID:
        case PARAM_INT:
          sprintf(buf, codes[i].description, paramid);
          break;
        case PARAM_POOL:
          sprintf(buf, codes[i].description, paramid, pools[paramid]->rpc_url);
          break;
        case PARAM_GPUMAX:
          sprintf(buf, codes[i].description, paramid, nDevs - 1);
          break;
        case PARAM_PMAX:
          sprintf(buf, codes[i].description, total_pools);
          break;
        case PARAM_PRMAX:
          sprintf(buf, codes[i].description, total_profiles);
          break;
        case PARAM_POOLMAX:
          sprintf(buf, codes[i].description, paramid, total_pools - 1);
          break;
        case PARAM_DMAX:
          sprintf(buf, codes[i].description, nDevs);
          break;
        case PARAM_CMD:
          sprintf(buf, codes[i].description, JSON_COMMAND);
          break;
        case PARAM_STR:
          sprintf(buf, codes[i].description, param2);
          break;
        case PARAM_BOTH:
          sprintf(buf, codes[i].description, paramid, param2);
          break;
        case PARAM_BOOL:
          sprintf(buf, codes[i].description, paramid ? TRUESTR : FALSESTR);
          break;
        case PARAM_SET:
          sprintf(buf, codes[i].description, param2, paramid);
          break;
        case PARAM_NONE:
        default:
          strcpy(buf, codes[i].description);
      }

      root = api_add_string(root, _STATUS, severity, false);
      root = api_add_time(root, "When", &when, false);
      root = api_add_int(root, "Code", &messageid, false);
      root = api_add_escape(root, "Msg", buf, false);
      root = api_add_escape(root, "Description", opt_api_description, false);

      root = print_data(root, buf2, isjson, false);
      io_add(io_data, buf2);
      if (isjson)
        io_add(io_data, JSON_CLOSE);
      return;
    }
  }

  root = api_add_string(root, _STATUS, "F", false);
  root = api_add_time(root, "When", &when, false);
  int id = -1;
  root = api_add_int(root, "Code", &id, false);
  sprintf(buf, "%d", messageid);
  root = api_add_escape(root, "Msg", buf, false);
  root = api_add_escape(root, "Description", opt_api_description, false);

  root = print_data(root, buf2, isjson, false);
  io_add(io_data, buf2);
  if (isjson)
    io_add(io_data, JSON_CLOSE);
}

#if LOCK_TRACKING

static uint64_t lock_id = 1;

static LOCKLIST *lockhead;

static void lockmsgnow()
{
  struct timeval now;
  struct tm *tm;
  time_t dt;

  cgtime(&now);

  dt = now.tv_sec;
  tm = localtime(&dt);

  LOCKMSG("%d-%02d-%02d %02d:%02d:%02d",
    tm->tm_year + 1900,
    tm->tm_mon + 1,
    tm->tm_mday,
    tm->tm_hour,
    tm->tm_min,
    tm->tm_sec);
}

static LOCKLIST *newlock(void *lock, enum cglock_typ typ, const char *file, const char *func, const int linenum)
{
  LOCKLIST *list;

  list = (LOCKLIST *)calloc(1, sizeof(*list));
  if (!list)
    quithere(1, "OOM list");
  list->info = (LOCKINFO *)calloc(1, sizeof(*(list->info)));
  if (!list->info)
    quithere(1, "OOM info");
  list->next = lockhead;
  lockhead = list;

  list->info->lock = lock;
  list->info->typ = typ;
  list->info->file = file;
  list->info->func = func;
  list->info->linenum = linenum;

  return list;
}

static LOCKINFO *findlock(void *lock, enum cglock_typ typ, const char *file, const char *func, const int linenum)
{
  LOCKLIST *look;

  look = lockhead;
  while (look) {
    if (look->info->lock == lock)
      break;
    look = look->next;
  }

  if (!look)
    look = newlock(lock, typ, file, func, linenum);

  return look->info;
}

static void addgettry(LOCKINFO *info, uint64_t id, const char *file, const char *func, const int linenum, bool get)
{
  LOCKSTAT *stat;
  LOCKLINE *line;

  stat = (LOCKSTAT *)calloc(1, sizeof(*stat));
  if (!stat)
    quithere(1, "OOM stat");
  line = (LOCKLINE *)calloc(1, sizeof(*line));
  if (!line)
    quithere(1, "OOM line");

  if (get)
    info->gets++;
  else
    info->tries++;

  stat->lock_id = id;
  stat->file = file;
  stat->func = func;
  stat->linenum = linenum;
  cgtime(&stat->tv);

  line->stat = stat;

  if (get) {
    line->next = info->lockgets;
    if (info->lockgets)
      info->lockgets->prev = line;
    info->lockgets = line;
  } else {
    line->next = info->locktries;
    if (info->locktries)
      info->locktries->prev = line;
    info->locktries = line;
  }
}

static void markgotdid(LOCKINFO *info, uint64_t id, const char *file, const char *func, const int linenum, bool got, int ret)
{
  LOCKLINE *line;

  if (got)
    info->gots++;
  else {
    if (ret == 0)
      info->dids++;
    else
      info->didnts++;
  }

  if (got || ret == 0) {
    info->lastgot.lock_id = id;
    info->lastgot.file = file;
    info->lastgot.func = func;
    info->lastgot.linenum = linenum;
    cgtime(&info->lastgot.tv);
  }

  if (got)
    line = info->lockgets;
  else
    line = info->locktries;
  while (line) {
    if (line->stat->lock_id == id)
      break;
    line = line->next;
  }

  if (!line) {
    lockmsgnow();
    LOCKMSGFFL("ERROR attempt to mark a lock as '%s' that wasn't '%s' id=%"PRIu64,
        got ? "got" : "did/didnt", got ? "get" : "try", id);
  }

  // Unlink it
  if (line->prev)
    line->prev->next = line->next;
  if (line->next)
    line->next->prev = line->prev;

  if (got) {
    if (info->lockgets == line)
      info->lockgets = line->next;
  } else {
    if (info->locktries == line)
      info->locktries = line->next;
  }

  free(line->stat);
  free(line);
}

// Yes this uses locks also ... ;/
static void locklock()
{
  if (unlikely(pthread_mutex_lock(&lockstat_lock)))
    quithere(1, "WTF MUTEX ERROR ON LOCK! errno=%d", errno);
}

static void lockunlock()
{
  if (unlikely(pthread_mutex_unlock(&lockstat_lock)))
    quithere(1, "WTF MUTEX ERROR ON UNLOCK! errno=%d", errno);
}

uint64_t api_getlock(void *lock, const char *file, const char *func, const int linenum)
{
  LOCKINFO *info;
  uint64_t id;

  locklock();

  info = findlock(lock, CGLOCK_UNKNOWN, file, func, linenum);
  id = lock_id++;
  addgettry(info, id, file, func, linenum, true);

  lockunlock();

  return id;
}

void api_gotlock(uint64_t id, void *lock, const char *file, const char *func, const int linenum)
{
  LOCKINFO *info;

  locklock();

  info = findlock(lock, CGLOCK_UNKNOWN, file, func, linenum);
  markgotdid(info, id, file, func, linenum, true, 0);

  lockunlock();
}

uint64_t api_trylock(void *lock, const char *file, const char *func, const int linenum)
{
  LOCKINFO *info;
  uint64_t id;

  locklock();

  info = findlock(lock, CGLOCK_UNKNOWN, file, func, linenum);
  id = lock_id++;
  addgettry(info, id, file, func, linenum, false);

  lockunlock();

  return id;
}

void api_didlock(uint64_t id, int ret, void *lock, const char *file, const char *func, const int linenum)
{
  LOCKINFO *info;

  locklock();

  info = findlock(lock, CGLOCK_UNKNOWN, file, func, linenum);
  markgotdid(info, id, file, func, linenum, false, ret);

  lockunlock();
}

void api_gunlock(void *lock, const char *file, const char *func, const int linenum)
{
  LOCKINFO *info;

  locklock();

  info = findlock(lock, CGLOCK_UNKNOWN, file, func, linenum);
  info->unlocks++;

  lockunlock();
}

void api_initlock(void *lock, enum cglock_typ typ, const char *file, const char *func, const int linenum)
{
  locklock();

  findlock(lock, typ, file, func, linenum);

  lockunlock();
}

void dsp_det(char *msg, LOCKSTAT *stat)
{
  struct tm *tm;
  time_t dt;

  dt = stat->tv.tv_sec;
  tm = localtime(&dt);

  LOCKMSGMORE("%s id=%"PRIu64" by %s %s():%d at %d-%02d-%02d %02d:%02d:%02d",
      msg,
      stat->lock_id,
      stat->file,
      stat->func,
      stat->linenum,
      tm->tm_year + 1900,
      tm->tm_mon + 1,
      tm->tm_mday,
      tm->tm_hour,
      tm->tm_min,
      tm->tm_sec);
}

void dsp_lock(LOCKINFO *info)
{
  LOCKLINE *line;
  char *status;

  LOCKMSG("Lock %p created by %s %s():%d",
    info->lock,
    info->file,
    info->func,
    info->linenum);
  LOCKMSGMORE("gets:%"PRIu64" gots:%"PRIu64" tries:%"PRIu64
        " dids:%"PRIu64" didnts:%"PRIu64" unlocks:%"PRIu64,
      info->gets,
      info->gots,
      info->tries,
      info->dids,
      info->didnts,
      info->unlocks);

  if (info->gots > 0 || info->dids > 0) {
    if (info->unlocks < info->gots + info->dids)
      status = "Last got/did still HELD";
    else
      status = "Last got/did (idle)";

    dsp_det(status, &(info->lastgot));
  } else
    LOCKMSGMORE("... unused ...");

  if (info->lockgets) {
    LOCKMSGMORE("BLOCKED gets (%"PRIu64")", info->gets - info->gots);
    line = info->lockgets;
    while (line) {
      dsp_det("", line->stat);
      line = line->next;
    }
  } else
    LOCKMSGMORE("no blocked gets");

  if (info->locktries) {
    LOCKMSGMORE("BLOCKED tries (%"PRIu64")", info->tries - info->dids - info->didnts);
    line = info->lockgets;
    while (line) {
      dsp_det("", line->stat);
      line = line->next;
    }
  } else
    LOCKMSGMORE("no blocked tries");
}

void show_locks()
{
  LOCKLIST *list;

  locklock();

  lockmsgnow();

  list = lockhead;
  if (!list)
    LOCKMSG("no locks?!?\n");
  else {
    while (list) {
      dsp_lock(list->info);
      list = list->next;
    }
  }

  LOCKMSGFLUSH();

  lockunlock();
}
#endif

static void copyadvanceafter(char ch, char **param, char **buf)
{
#define src_p (*param)
#define dst_b (*buf)

  while (*src_p && *src_p != ch) {
    if (*src_p == '\\' && *(src_p+1) != '\0')
      src_p++;

    *(dst_b++) = *(src_p++);
  }
  if (*src_p)
    src_p++;

  *(dst_b++) = '\0';
}

static void lockstats(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#if LOCK_TRACKING
  show_locks();
  message(io_data, MSG_LOCKOK, 0, NULL, isjson);
#else
  message(io_data, MSG_LOCKDIS, 0, NULL, isjson);
#endif
}

static void apiversion(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open;

  message(io_data, MSG_VERSION, 0, NULL, isjson);
  io_open = io_add(io_data, isjson ? COMSTR JSON_VERSION : _VERSION COMSTR);

  root = api_add_string(root, "Miner", PACKAGE " " VERSION, false);
  root = api_add_string(root, "SGMiner", CGMINER_VERSION, false);
  root = api_add_const(root, "API", APIVERSION, false);

  root = print_data(root, buf, isjson, false);
  io_add(io_data, buf);
  if (isjson && io_open)
    io_close(io_data);
}

static void minerconfig(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open;
  int gpucount = 0;
  char *adlinuse = (char *)NO;
#ifdef HAVE_ADL
  const char *adl = YES;
  int i;

  for (i = 0; i < nDevs; i++) {
    if (gpus[i].has_adl) {
      adlinuse = (char *)YES;
      break;
    }
  }
#else
  const char *adl = NO;
#endif

  gpucount = nDevs;

  message(io_data, MSG_MINECONFIG, 0, NULL, isjson);
  io_open = io_add(io_data, isjson ? COMSTR JSON_MINECONFIG : _MINECONFIG COMSTR);

  root = api_add_int(root, "GPU Count", &gpucount, false);
  root = api_add_int(root, "Pool Count", &total_pools, false);
  root = api_add_const(root, "ADL", (char *)adl, false);
  root = api_add_string(root, "ADL in use", adlinuse, false);
  root = api_add_const(root, "Strategy", strategies[pool_strategy].s, false);
  root = api_add_int(root, "Rotate Period", &opt_rotate_period, false);
  root = api_add_int(root, "Log Interval", &opt_log_interval, false);
  root = api_add_const(root, "Device Code", DEVICECODE, false);
  root = api_add_const(root, "OS", OSINFO, false);
  root = api_add_bool(root, "Failover-Only", &opt_fail_only, false);
  root = api_add_int(root, "Failover Switch Delay", &opt_fail_switch_delay, false);
  root = api_add_int(root, "ScanTime", &opt_scantime, false);
  root = api_add_int(root, "Queue", &opt_queue, false);
  root = api_add_int(root, "Expiry", &opt_expiry, false);

  root = print_data(root, buf, isjson, false);
  io_add(io_data, buf);
  if (isjson && io_open)
    io_close(io_data);
}

static const char *status2str(enum alive status)
{
  switch (status) {
    case LIFE_WELL:
      return ALIVE;
    case LIFE_SICK:
      return SICK;
    case LIFE_DEAD:
      return DEAD;
    case LIFE_NOSTART:
      return NOSTART;
    case LIFE_INIT:
      return INIT;
    default:
      return UNKNOWN;
  }
}

static void gpustatus(struct io_data *io_data, int gpu, bool isjson, bool precom)
{
  struct api_data *root = NULL;
  char intensity[20];
  char buf[TMPBUFSIZ];
  char *enabled;
  char *status;
  float gt, gv;
  int ga, gf, gp, gc, gm, pt;

  if (gpu >= 0 && gpu < nDevs) {
    struct cgpu_info *cgpu = &gpus[gpu];
    double dev_runtime;

    dev_runtime = cgpu_runtime(cgpu);

    cgpu->utility = cgpu->accepted / dev_runtime * 60;

#ifdef HAVE_ADL
    if (!gpu_stats(gpu, &gt, &gc, &gm, &gv, &ga, &gf, &gp, &pt))
#endif
      gt = gv = gm = gc = ga = gf = gp = pt = 0;

    if (cgpu->deven != DEV_DISABLED)
      enabled = (char *)YES;
    else
      enabled = (char *)NO;

    status = (char *)status2str(cgpu->status);

    if (cgpu->dynamic)
      strcpy(intensity, DYNAMIC);
    else
      sprintf(intensity, "%d", cgpu->intensity);

    root = api_add_int(root, "GPU", &gpu, false);
    root = api_add_string(root, "Enabled", enabled, false);
    root = api_add_string(root, "Status", status, false);
    root = api_add_temp(root, "Temperature", &gt, false);
    root = api_add_int(root, "Fan Speed", &gf, false);
    root = api_add_int(root, "Fan Percent", &gp, false);
    root = api_add_int(root, "GPU Clock", &gc, false);
    root = api_add_int(root, "Memory Clock", &gm, false);
    root = api_add_volts(root, "GPU Voltage", &gv, false);
    root = api_add_int(root, "GPU Activity", &ga, false);
    root = api_add_int(root, "Powertune", &pt, false);
    double mhs = cgpu->total_mhashes / total_secs;
    root = api_add_mhs(root, "MHS av", &mhs, false);
    char mhsname[27];
    sprintf(mhsname, "MHS %ds", opt_log_interval);
    root = api_add_mhs(root, mhsname, &(cgpu->rolling), false);
    double khs_avg = mhs * 1000.0;
    double khs_rolling = cgpu->rolling * 1000.0;
    root = api_add_khs(root, "KHS av", &khs_avg, false);
    char khsname[27];
    sprintf(khsname, "KHS %ds", opt_log_interval);
    root = api_add_khs(root, khsname, &khs_rolling, false);
    root = api_add_int(root, "Accepted", &(cgpu->accepted), false);
    root = api_add_int(root, "Rejected", &(cgpu->rejected), false);
    root = api_add_int(root, "Hardware Errors", &(cgpu->hw_errors), false);
    root = api_add_utility(root, "Utility", &(cgpu->utility), false);
    root = api_add_string(root, "Intensity", intensity, false);
    root = api_add_int(root, "XIntensity", &(cgpu->xintensity), false);
    root = api_add_int(root, "RawIntensity", &(cgpu->rawintensity), false);
    int last_share_pool = cgpu->last_share_pool_time > 0 ?
          cgpu->last_share_pool : -1;
    root = api_add_int(root, "Last Share Pool", &last_share_pool, false);
    root = api_add_time(root, "Last Share Time", &(cgpu->last_share_pool_time), false);
    root = api_add_mhtotal(root, "Total MH", &(cgpu->total_mhashes), false);
    root = api_add_double(root, "Diff1 Work", &(cgpu->diff1), false);
    root = api_add_diff(root, "Difficulty Accepted", &(cgpu->diff_accepted), false);
    root = api_add_diff(root, "Difficulty Rejected", &(cgpu->diff_rejected), false);
    root = api_add_diff(root, "Last Share Difficulty", &(cgpu->last_share_diff), false);
    root = api_add_time(root, "Last Valid Work", &(cgpu->last_device_valid_work), false);
    double hwp = (cgpu->hw_errors + cgpu->diff1) ?
        (double)(cgpu->hw_errors) / (double)(cgpu->hw_errors + cgpu->diff1) : 0;
    root = api_add_percent(root, "Device Hardware%", &hwp, false);
    double rejp = cgpu->diff1 ?
        (double)(cgpu->diff_rejected) / (double)(cgpu->diff1) : 0;
    root = api_add_percent(root, "Device Rejected%", &rejp, false);
    root = api_add_elapsed(root, "Device Elapsed", &(total_secs), true); // GPUs don't hotplug

    root = print_data(root, buf, isjson, precom);
    io_add(io_data, buf);
  }
}

static void devstatus(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  bool io_open = false;
  int devcount = 0;
  int numgpu = 0;
  int i;
  numgpu = nDevs;

  if (numgpu == 0) {
    message(io_data, MSG_NODEVS, 0, NULL, isjson);
    return;
  }


  message(io_data, MSG_DEVS, 0, NULL, isjson);
  if (isjson)
    io_open = io_add(io_data, COMSTR JSON_DEVS);

  for (i = 0; i < nDevs; i++) {
    gpustatus(io_data, i, isjson, isjson && devcount > 0);

    devcount++;
  }
  if (isjson && io_open)
    io_close(io_data);
}

static void gpudev(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  bool io_open = false;
  int id;

  if (nDevs == 0) {
    message(io_data, MSG_GPUNON, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  if (id < 0 || id >= nDevs) {
    message(io_data, MSG_INVGPU, id, NULL, isjson);
    return;
  }

  message(io_data, MSG_GPUDEV, id, NULL, isjson);

  if (isjson)
    io_open = io_add(io_data, COMSTR JSON_GPU);

  gpustatus(io_data, id, isjson, false);

  if (isjson && io_open)
    io_close(io_data);
}

static void poolstatus(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open = false;
  char *status, *lp;
  int i;

  if (total_pools == 0) {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  message(io_data, MSG_POOL, 0, NULL, isjson);

  if (isjson)
    io_open = io_add(io_data, COMSTR JSON_POOLS);

  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];

    if (pool->removed)
      continue;

    switch (pool->state) {
      case POOL_DISABLED:
        status = (char *)DISABLED;
        break;
      case POOL_REJECTING:
        status = (char *)REJECTING;
        break;
      case POOL_ENABLED:
        if (pool->idle)
          status = (char *)DEAD;
        else
          status = (char *)ALIVE;
        break;
      default:
        status = (char *)UNKNOWN;
        break;
    }

    if (pool->hdr_path)
      lp = (char *)YES;
    else
      lp = (char *)NO;

    root = api_add_int(root, "POOL", &i, false);
    mutex_lock(&pool->stratum_lock);
    root = api_add_string(root, "Name", get_pool_name(pool), true);
    mutex_unlock(&pool->stratum_lock);
    root = api_add_escape(root, "URL", pool->rpc_url, false);
    root = api_add_escape(root, "Profile", pool->profile, false);
    root = api_add_escape(root, "Algorithm", pool->algorithm.name, false);
    root = api_add_escape(root, "Algorithm Type", (char *)algorithm_type_str[pool->algorithm.type], false);

    //show nfactor for nscrypt
    if(pool->algorithm.type == ALGO_NSCRYPT)
      root = api_add_int(root, "Algorithm NFactor", (int *)&(pool->algorithm.nfactor), false);

    root = api_add_string(root, "Description", pool->description, false);
    root = api_add_string(root, "Status", status, false);
    root = api_add_int(root, "Priority", &(pool->prio), false);
    root = api_add_int(root, "Quota", &pool->quota, false);
    root = api_add_string(root, "Long Poll", lp, false);
    root = api_add_uint(root, "Getworks", &(pool->getwork_requested), false);
    root = api_add_int(root, "Accepted", &(pool->accepted), false);
    root = api_add_int(root, "Rejected", &(pool->rejected), false);
    root = api_add_int(root, "Works", &pool->works, false);
    root = api_add_uint(root, "Discarded", &(pool->discarded_work), false);
    root = api_add_uint(root, "Stale", &(pool->stale_shares), false);
    root = api_add_uint(root, "Get Failures", &(pool->getfail_occasions), false);
    root = api_add_uint(root, "Remote Failures", &(pool->remotefail_occasions), false);
    root = api_add_escape(root, "User", pool->rpc_user, false);
    root = api_add_time(root, "Last Share Time", &(pool->last_share_time), false);
    root = api_add_double(root, "Diff1 Shares", &(pool->diff1), false);

    if (pool->rpc_proxy) {
      root = api_add_const(root, "Proxy Type", proxytype(pool->rpc_proxytype), false);
      root = api_add_escape(root, "Proxy", pool->rpc_proxy, false);
    } else {
      root = api_add_const(root, "Proxy Type", BLANK, false);
      root = api_add_const(root, "Proxy", BLANK, false);
    }
    root = api_add_diff(root, "Difficulty Accepted", &(pool->diff_accepted), false);
    root = api_add_diff(root, "Difficulty Rejected", &(pool->diff_rejected), false);
    root = api_add_diff(root, "Difficulty Stale", &(pool->diff_stale), false);
    root = api_add_diff(root, "Last Share Difficulty", &(pool->last_share_diff), false);
    root = api_add_bool(root, "Has Stratum", &(pool->has_stratum), false);
    root = api_add_bool(root, "Stratum Active", &(pool->stratum_active), false);
    if (pool->stratum_active)
      root = api_add_escape(root, "Stratum URL", pool->stratum_url, false);
    else
      root = api_add_const(root, "Stratum URL", BLANK, false);
    root = api_add_bool(root, "Has GBT", &(pool->has_gbt), false);
    root = api_add_double(root, "Best Share", &(pool->best_diff), true);
    double rejp = (pool->diff_accepted + pool->diff_rejected + pool->diff_stale) ?
        (double)(pool->diff_rejected) / (double)(pool->diff_accepted + pool->diff_rejected + pool->diff_stale) : 0;
    root = api_add_percent(root, "Pool Rejected%", &rejp, false);
    double stalep = (pool->diff_accepted + pool->diff_rejected + pool->diff_stale) ?
        (double)(pool->diff_stale) / (double)(pool->diff_accepted + pool->diff_rejected + pool->diff_stale) : 0;
    root = api_add_percent(root, "Pool Stale%", &stalep, false);

    root = print_data(root, buf, isjson, isjson && (i > 0));
    io_add(io_data, buf);
  }

  if (isjson && io_open)
    io_close(io_data);
}

static void summary(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open;
  double utility, mhs, work_utility;

  message(io_data, MSG_SUMM, 0, NULL, isjson);
  io_open = io_add(io_data, isjson ? COMSTR JSON_SUMMARY : _SUMMARY COMSTR);

  // stop hashmeter() changing some while copying
  mutex_lock(&hash_lock);

  utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
  mhs = total_mhashes_done / total_secs;
  work_utility = total_diff1 / ( total_secs ? total_secs : 1 ) * 60;

  root = api_add_elapsed(root, "Elapsed", &(total_secs), true);
  root = api_add_mhs(root, "MHS av", &(mhs), false);
  char mhsname[27];
  sprintf(mhsname, "MHS %ds", opt_log_interval);
  root = api_add_mhs(root, mhsname, &(total_rolling), false);
  double khs_avg = mhs * 1000.0;
  double khs_rolling = total_rolling * 1000.0;
  root = api_add_khs(root, "KHS av", &khs_avg, false);
  char khsname[27];
  sprintf(khsname, "KHS %ds", opt_log_interval);
  root = api_add_khs(root, khsname, &khs_rolling, false);
  root = api_add_uint(root, "Found Blocks", &(found_blocks), true);
  root = api_add_int(root, "Getworks", &(total_getworks), true);
  root = api_add_int(root, "Accepted", &(total_accepted), true);
  root = api_add_int(root, "Rejected", &(total_rejected), true);
  root = api_add_int(root, "Hardware Errors", &(hw_errors), true);
  root = api_add_utility(root, "Utility", &(utility), false);
  root = api_add_int(root, "Discarded", &(total_discarded), true);
  root = api_add_int(root, "Stale", &(total_stale), true);
  root = api_add_uint(root, "Get Failures", &(total_go), true);
  root = api_add_uint(root, "Local Work", &(local_work), true);
  root = api_add_uint(root, "Remote Failures", &(total_ro), true);
  root = api_add_uint(root, "Network Blocks", &(new_blocks), true);
  root = api_add_mhtotal(root, "Total MH", &(total_mhashes_done), true);
  root = api_add_utility(root, "Work Utility", &(work_utility), false);
  root = api_add_diff(root, "Difficulty Accepted", &(total_diff_accepted), true);
  root = api_add_diff(root, "Difficulty Rejected", &(total_diff_rejected), true);
  root = api_add_diff(root, "Difficulty Stale", &(total_diff_stale), true);
  root = api_add_double(root, "Best Share", &(best_diff), true);
  double hwp = (hw_errors + total_diff1) ?
      (double)(hw_errors) / (double)(hw_errors + total_diff1) : 0;
  root = api_add_percent(root, "Device Hardware%", &hwp, false);
  double rejp = total_diff1 ?
      (double)(total_diff_rejected) / (double)(total_diff1) : 0;
  root = api_add_percent(root, "Device Rejected%", &rejp, false);
  double prejp = (total_diff_accepted + total_diff_rejected + total_diff_stale) ?
      (double)(total_diff_rejected) / (double)(total_diff_accepted + total_diff_rejected + total_diff_stale) : 0;
  root = api_add_percent(root, "Pool Rejected%", &prejp, false);
  double stalep = (total_diff_accepted + total_diff_rejected + total_diff_stale) ?
      (double)(total_diff_stale) / (double)(total_diff_accepted + total_diff_rejected + total_diff_stale) : 0;
  root = api_add_percent(root, "Pool Stale%", &stalep, false);
  root = api_add_time(root, "Last getwork", &last_getwork, false);

  mutex_unlock(&hash_lock);

  root = print_data(root, buf, isjson, false);
  io_add(io_data, buf);
  if (isjson && io_open)
    io_close(io_data);
}

static void gpuenable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  struct thr_info *thr;
  int gpu;
  int id;
  int i;

  if (mining_threads == 0) {
    message(io_data, MSG_GPUNON, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  if (id < 0 || id >= nDevs) {
    message(io_data, MSG_INVGPU, id, NULL, isjson);
    return;
  }

  applog(LOG_DEBUG, "API: request to gpuenable gpuid %d %s%u",
      id, gpus[id].drv->name, gpus[id].device_id);

  if (gpus[id].deven != DEV_DISABLED) {
    message(io_data, MSG_ALRENA, id, NULL, isjson);
    return;
  }

  rd_lock(&mining_thr_lock);
  for (i = 0; i < mining_threads; i++) {
    thr = mining_thr[i];
    gpu = thr->cgpu->device_id;
    if (gpu == id) {
      if (thr->cgpu->status != LIFE_WELL) {
        message(io_data, MSG_GPUMRE, id, NULL, isjson);
        return;
      }
      gpus[id].deven = DEV_ENABLED;
      applog(LOG_DEBUG, "API Pushing sem post to thread %d", thr->id);
      cgsem_post(&thr->sem);
    }
  }
  rd_unlock(&mining_thr_lock);

  message(io_data, MSG_GPUREN, id, NULL, isjson);
}

static void gpudisable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  int id;

  if (nDevs == 0) {
    message(io_data, MSG_GPUNON, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  if (id < 0 || id >= nDevs) {
    message(io_data, MSG_INVGPU, id, NULL, isjson);
    return;
  }

  applog(LOG_DEBUG, "API: request to gpudisable gpuid %d %s%u",
      id, gpus[id].drv->name, gpus[id].device_id);

  if (gpus[id].deven == DEV_DISABLED) {
    message(io_data, MSG_ALRDIS, id, NULL, isjson);
    return;
  }

  gpus[id].deven = DEV_DISABLED;

  message(io_data, MSG_GPUDIS, id, NULL, isjson);
}

static void gpurestart(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  int id;

  if (nDevs == 0) {
    message(io_data, MSG_GPUNON, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  if (id < 0 || id >= nDevs) {
    message(io_data, MSG_INVGPU, id, NULL, isjson);
    return;
  }

  reinit_device(&gpus[id]);

  message(io_data, MSG_GPUREI, id, NULL, isjson);
}

static void gpucount(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open;
  int numgpu = 0;
  numgpu = nDevs;

  message(io_data, MSG_NUMGPU, 0, NULL, isjson);
  io_open = io_add(io_data, isjson ? COMSTR JSON_GPUS : _GPUS COMSTR);

  root = api_add_int(root, "Count", &numgpu, false);

  root = print_data(root, buf, isjson, false);
  io_add(io_data, buf);
  if (isjson && io_open)
    io_close(io_data);
}

static void switchpool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  struct pool *pool;
  int id;

  if (total_pools == 0) {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISPID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  cg_rlock(&control_lock);
  if (id < 0 || id >= total_pools) {
    cg_runlock(&control_lock);
    message(io_data, MSG_INVPID, id, NULL, isjson);
    return;
  }

  pool = pools[id];
  pool->state = POOL_ENABLED;
  cg_runlock(&control_lock);
  switch_pools(pool);

  message(io_data, MSG_SWITCHP, id, NULL, isjson);
}

static void api_pool_strategy(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  char *p;
  int strategy;

  if (total_pools == 0)
  {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0')
  {
    message(io_data, MSG_MISSTRAT, 0, NULL, isjson);
    return;
  }

  //get strategy in parameter 1
  if(!(p = strtok(param, ",")))
  {
    message(io_data, MSG_MISSTRAT, 0, NULL, isjson);
    return;
  }

  strategy = atoi(p);

  //invalid strategy
  if(strategy < 0 || strategy > TOP_STRATEGY)
  {
    message(io_data, MSG_INVSTRAT, strategy, NULL, isjson);
    return;
  }

  //if set to rotate, get second param
  if(strategy == POOL_ROTATE)
  {
    //if second param is missing then invalid
    if(!(p = strtok(NULL, ",")))
    {
      message(io_data, MSG_MISSTRATINT, 0, NULL, isjson);
      return;
    }

    //get interval in parameter 2
    int interval;
    interval = atoi(p);

    //interval can only be between 0 and 9999
    if(interval < 0 || interval > 9999)
    {
      message(io_data, MSG_INVNUM, interval, "interval", isjson);
      return;
    }

    //set interval
    opt_rotate_period = interval;
  }

  //set pool strategy
  pool_strategy = (enum pool_strategy)strategy;
  //initiate new strategy
  switch_pools(NULL);

  message(io_data, MSG_CHSTRAT, 0, (char *)strategies[strategy].s, isjson);
}

static bool pooldetails(char *param, char **url, char **user, char **pass,
      char **name, char **desc, char **profile, char **algo)
{
  char *ptr, *buf;

  ptr = buf = (char *)malloc(strlen(param) + 1);
  if (unlikely(!buf))
    quit(1, "Failed to malloc pooldetails buf");

  *url = buf;
  copyadvanceafter(',', &param, &buf);
  if (!(*param)) // missing user
    goto exitsama;

  *user = buf;
  copyadvanceafter(',', &param, &buf);
  if (!(*param)) // missing pass
    goto exitsama;

  *pass = buf;
  copyadvanceafter(',', &param, &buf);
  if (!(*param)) // missing name (allowed)
    return true;

  *name = buf;
  copyadvanceafter(',', &param, &buf);
  if (!(*param)) // missing desc
    return true;

  *desc = buf;
  copyadvanceafter(',', &param, &buf);
  if (!(*param)) // missing profile
    return true;

  *profile = buf;
  copyadvanceafter(',', &param, &buf);
  if (!(*param)) // missing algo
    return true;

  *algo = buf;
  copyadvanceafter(',', &param, &buf);

  return true;

exitsama:
  free(ptr);
  return false;
}

static void addpool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  char *url, *user, *pass;
  char *name = NULL, *desc = NULL, *algo = NULL, *profile = NULL;
  struct pool *pool;
  char *ptr;

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISPDP, 0, NULL, isjson);
    return;
  }

  if (!pooldetails(param, &url, &user, &pass,
       &name, &desc, &profile, &algo)) {
    ptr = escape_string(param, isjson);
    message(io_data, MSG_INVPDP, 0, ptr, isjson);
    if (ptr != param)
      free(ptr);
    ptr = NULL;
    return;
  }

  /* If API client is old, it might not have provided all fields. */
      name = ((name == NULL)?strdup(""):name);
      desc = ((desc == NULL)?strdup(""):desc);
      profile = ((profile == NULL)?strdup(""):profile);
      algo = ((algo == NULL)?strdup(""):algo);

  pool = add_pool();
  detect_stratum(pool, url);
  add_pool_details(pool, true, url, user, pass, name, desc, profile, algo);

  ptr = escape_string(url, isjson);
  message(io_data, MSG_ADDPOOL, 0, ptr, isjson);
  if (ptr != url)
    free(ptr);
  ptr = NULL;
}

static void enablepool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  struct pool *pool;
  int id;

  if (total_pools == 0) {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISPID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  if (id < 0 || id >= total_pools) {
    message(io_data, MSG_INVPID, id, NULL, isjson);
    return;
  }

  pool = pools[id];
  if (pool->state == POOL_ENABLED) {
    message(io_data, MSG_ALRENAP, id, NULL, isjson);
    return;
  }

  pool->state = POOL_ENABLED;
  if (pool->prio < current_pool()->prio)
    switch_pools(pool);

  message(io_data, MSG_ENAPOOL, id, NULL, isjson);
}

static void poolpriority(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  char *ptr, *next;
  int i, pr, prio = 0;

  // TODO: all sgminer code needs a mutex added everywhere for change
  //  access to total_pools and also parts of the pools[] array,
  //  just copying total_pools here wont solve that

  if (total_pools == 0) {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISPID, 0, NULL, isjson);
    return;
  }

  bool* pools_changed = (bool*)alloca(total_pools*sizeof(bool));
  int* new_prio = (int*)alloca(total_pools*sizeof(int));

  for (i = 0; i < total_pools; ++i)
    pools_changed[i] = false;

  next = param;
  while (next && *next) {
    ptr = next;
    next = strchr(ptr, ',');
    if (next)
      *(next++) = '\0';

    i = atoi(ptr);
    if (i < 0 || i >= total_pools) {
      message(io_data, MSG_INVPID, i, NULL, isjson);
      return;
    }

    if (pools_changed[i]) {
      message(io_data, MSG_DUPPID, i, NULL, isjson);
      return;
    }

    pools_changed[i] = true;
    new_prio[i] = prio++;
  }

  // Only change them if no errors
  for (i = 0; i < total_pools; i++) {
    if (pools_changed[i])
      pools[i]->prio = new_prio[i];
  }

  // In priority order, cycle through the unchanged pools and append them
  for (pr = 0; pr < total_pools; pr++)
    for (i = 0; i < total_pools; i++) {
      if (!pools_changed[i] && pools[i]->prio == pr) {
        pools[i]->prio = prio++;
        pools_changed[i] = true;
        break;
      }
    }

  if (current_pool()->prio)
    switch_pools(NULL);

  message(io_data, MSG_POOLPRIO, 0, NULL, isjson);
}

static void poolquota(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  struct pool *pool;
  int quota, id;
  char *comma;

  if (total_pools == 0) {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISPID, 0, NULL, isjson);
    return;
  }

  comma = strchr(param, ',');
  if (!comma) {
    message(io_data, MSG_CONVAL, 0, param, isjson);
    return;
  }

  *(comma++) = '\0';

  id = atoi(param);
  if (id < 0 || id >= total_pools) {
    message(io_data, MSG_INVPID, id, NULL, isjson);
    return;
  }
  pool = pools[id];

  quota = atoi(comma);
  if (quota < 0) {
    message(io_data, MSG_INVNEG, quota, pool->rpc_url, isjson);
    return;
  }

  pool->quota = quota;
  adjust_quota_gcd();
  message(io_data, MSG_SETQUOTA, quota, pool->rpc_url, isjson);
}

static void disablepool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  struct pool *pool;
  int id;

  if (total_pools == 0) {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISPID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  if (id < 0 || id >= total_pools) {
    message(io_data, MSG_INVPID, id, NULL, isjson);
    return;
  }

  pool = pools[id];
  if (pool->state == POOL_DISABLED) {
    message(io_data, MSG_ALRDISP, id, NULL, isjson);
    return;
  }

  pool->state = POOL_DISABLED;
  if (pool == current_pool())
    switch_pools(NULL);

  message(io_data, MSG_DISPOOL, id, NULL, isjson);
}

static void removepool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  struct pool *pool;
  char *rpc_url;
  bool dofree = false;
  int id;

  if (total_pools == 0) {
    message(io_data, MSG_NOPOOL, 0, NULL, isjson);
    return;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISPID, 0, NULL, isjson);
    return;
  }

  id = atoi(param);
  if (id < 0 || id >= total_pools) {
    message(io_data, MSG_INVPID, id, NULL, isjson);
    return;
  }

  if (total_pools <= 1) {
    message(io_data, MSG_REMLASTP, id, NULL, isjson);
    return;
  }

  pool = pools[id];
  if (pool == current_pool())
    switch_pools(NULL);

  if (pool == current_pool()) {
    message(io_data, MSG_ACTPOOL, id, NULL, isjson);
    return;
  }

  pool->state = POOL_DISABLED;
  rpc_url = escape_string(pool->rpc_url, isjson);
  if (rpc_url != pool->rpc_url)
    dofree = true;

  remove_pool(pool);

  message(io_data, MSG_REMPOOL, id, rpc_url, isjson);

  if (dofree)
    free(rpc_url);
  rpc_url = NULL;
}

static bool splitgpuvalue(struct io_data *io_data, char *param, int *gpu, char **value, bool isjson)
{
  int id;
  char *gpusep;

  if (nDevs == 0) {
    message(io_data, MSG_GPUNON, 0, NULL, isjson);
    return false;
  }

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISID, 0, NULL, isjson);
    return false;
  }

  gpusep = strchr(param, GPUSEP);
  if (gpusep == NULL) {
    message(io_data, MSG_MISVAL, 0, NULL, isjson);
    return false;
  }

  *(gpusep++) = '\0';

  id = atoi(param);
  if (id < 0 || id >= nDevs) {
    message(io_data, MSG_INVGPU, id, NULL, isjson);
    return false;
  }

  *gpu = id;
  *value = gpusep;

  return true;
}

static void gpuintensity(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  int id;
  char *value;
  int intensity;
  char intensitystr[7];

  if (!splitgpuvalue(io_data, param, &id, &value, isjson))
    return;

  if (!strncasecmp(value, DYNAMIC, 1)) {
    gpus[id].dynamic = true;
    strcpy(intensitystr, DYNAMIC);
  }
  else {
    intensity = atoi(value);
    if (intensity < MIN_INTENSITY || intensity > MAX_INTENSITY) {
      message(io_data, MSG_INVINT, 0, value, isjson);
      return;
    }

    gpus[id].dynamic = false;
    gpus[id].intensity = intensity;
    gpus[id].xintensity = 0;
    gpus[id].rawintensity = 0;
    sprintf(intensitystr, "%d", intensity);
  }

  // fix config with new settings so that we can save them
  update_config_intensity(get_gpu_profile(id));

  message(io_data, MSG_GPUINT, id, intensitystr, isjson);
}

static void gpuxintensity(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  int id;
  char *value;
  int intensity;
  char intensitystr[7];

  if (!splitgpuvalue(io_data, param, &id, &value, isjson))
    return;

  intensity = atoi(value);
  if (intensity < MIN_XINTENSITY || intensity > MAX_XINTENSITY) {
    message(io_data, MSG_INVXINT, 0, value, isjson);
    return;
  }

  gpus[id].dynamic = false;
  gpus[id].intensity = 0;
  gpus[id].xintensity = intensity;
  gpus[id].rawintensity = 0;
  sprintf(intensitystr, "%d", intensity);

  // fix config with new settings so that we can save them
  update_config_xintensity(get_gpu_profile(id));

  message(io_data, MSG_GPUXINT, id, intensitystr, isjson);
}

static void gpurawintensity(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  int id;
  char *value;
  int intensity;
  char intensitystr[16];

  if (!splitgpuvalue(io_data, param, &id, &value, isjson))
    return;

  intensity = atoi(value);
  if (intensity < MIN_RAWINTENSITY || intensity > MAX_RAWINTENSITY) {
    message(io_data, MSG_INVRAWINT, 0, value, isjson);
    return;
  }

  gpus[id].dynamic = false;
  gpus[id].intensity = 0;
  gpus[id].xintensity = 0;
  gpus[id].rawintensity = intensity;
  sprintf(intensitystr, "%d", intensity);

  // fix config with new settings so that we can save them
  update_config_rawintensity(get_gpu_profile(id));

  message(io_data, MSG_GPURAWINT, id, intensitystr, isjson);
}

static void gpumem(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
  int id;
  char *value;
  int clock;

  if (!splitgpuvalue(io_data, param, &id, &value, isjson))
    return;

  clock = atoi(value);

  if (set_memoryclock(id, clock))
    message(io_data, MSG_GPUMERR, id, value, isjson);
  else
    message(io_data, MSG_GPUMEM, id, value, isjson);
#else
  message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}

static void gpuengine(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
  int id;
  char *value;
  int clock;

  if (!splitgpuvalue(io_data, param, &id, &value, isjson))
    return;

  clock = atoi(value);

  if (set_engineclock(id, clock))
    message(io_data, MSG_GPUEERR, id, value, isjson);
  else
    message(io_data, MSG_GPUENG, id, value, isjson);
#else
  message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}

static void gpufan(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
  int id;
  char *value;
  int fan;

  if (!splitgpuvalue(io_data, param, &id, &value, isjson))
    return;

  fan = atoi(value);

  if (set_fanspeed(id, fan))
    message(io_data, MSG_GPUFERR, id, value, isjson);
  else
    message(io_data, MSG_GPUFAN, id, value, isjson);
#else
  message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}

static void gpuvddc(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
  int id;
  char *value;
  float vddc;

  if (!splitgpuvalue(io_data, param, &id, &value, isjson))
    return;

  vddc = atof(value);

  if (set_vddc(id, vddc))
    message(io_data, MSG_GPUVERR, id, value, isjson);
  else
    message(io_data, MSG_GPUVDDC, id, value, isjson);
#else
  message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}

void doquit(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  message(io_data, MSG_BYE, 0, _BYE, isjson);

  bye = true;
  do_a_quit = true;
}

void dorestart(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  message(io_data, MSG_BYE, 0, _RESTART, isjson);

  bye = true;
  do_a_restart = true;
}

void privileged(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  message(io_data, MSG_ACCOK, 0, NULL, isjson);
}

void notifystatus(struct io_data *io_data, int device, struct cgpu_info *cgpu, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  char *reason;

  if (cgpu->device_last_not_well == 0)
    reason = REASON_NONE;
  else
    switch(cgpu->device_not_well_reason) {
      case REASON_THREAD_FAIL_INIT:
        reason = REASON_THREAD_FAIL_INIT_STR;
        break;
      case REASON_THREAD_ZERO_HASH:
        reason = REASON_THREAD_ZERO_HASH_STR;
        break;
      case REASON_THREAD_FAIL_QUEUE:
        reason = REASON_THREAD_FAIL_QUEUE_STR;
        break;
      case REASON_DEV_SICK_IDLE_60:
        reason = REASON_DEV_SICK_IDLE_60_STR;
        break;
      case REASON_DEV_DEAD_IDLE_600:
        reason = REASON_DEV_DEAD_IDLE_600_STR;
        break;
      case REASON_DEV_NOSTART:
        reason = REASON_DEV_NOSTART_STR;
        break;
      case REASON_DEV_OVER_HEAT:
        reason = REASON_DEV_OVER_HEAT_STR;
        break;
      case REASON_DEV_THERMAL_CUTOFF:
        reason = REASON_DEV_THERMAL_CUTOFF_STR;
        break;
      case REASON_DEV_COMMS_ERROR:
        reason = REASON_DEV_COMMS_ERROR_STR;
        break;
      default:
        reason = REASON_UNKNOWN_STR;
        break;
    }

  // ALL counters (and only counters) must start the name with a '*'
  // Simplifies future external support for identifying new counters
  root = api_add_int(root, "NOTIFY", &device, false);
  root = api_add_string(root, "Name", cgpu->drv->name, false);
  root = api_add_int(root, "ID", &(cgpu->device_id), false);
  root = api_add_time(root, "Last Well", &(cgpu->device_last_well), false);
  root = api_add_time(root, "Last Not Well", &(cgpu->device_last_not_well), false);
  root = api_add_string(root, "Reason Not Well", reason, false);
  root = api_add_int(root, "*Thread Fail Init", &(cgpu->thread_fail_init_count), false);
  root = api_add_int(root, "*Thread Zero Hash", &(cgpu->thread_zero_hash_count), false);
  root = api_add_int(root, "*Thread Fail Queue", &(cgpu->thread_fail_queue_count), false);
  root = api_add_int(root, "*Dev Sick Idle 60s", &(cgpu->dev_sick_idle_60_count), false);
  root = api_add_int(root, "*Dev Dead Idle 600s", &(cgpu->dev_dead_idle_600_count), false);
  root = api_add_int(root, "*Dev Nostart", &(cgpu->dev_nostart_count), false);
  root = api_add_int(root, "*Dev Over Heat", &(cgpu->dev_over_heat_count), false);
  root = api_add_int(root, "*Dev Thermal Cutoff", &(cgpu->dev_thermal_cutoff_count), false);
  root = api_add_int(root, "*Dev Comms Error", &(cgpu->dev_comms_error_count), false);
  root = api_add_int(root, "*Dev Throttle", &(cgpu->dev_throttle_count), false);

  root = print_data(root, buf, isjson, isjson && (device > 0));
  io_add(io_data, buf);
}

static void notify(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, char group)
{
  struct cgpu_info *cgpu;
  bool io_open = false;
  int i, j;

  if (total_devices == 0) {
    message(io_data, MSG_NODEVS, 0, NULL, isjson);
    return;
  }

  message(io_data, MSG_NOTIFY, 0, NULL, isjson);

  if (isjson)
    io_open = io_add(io_data, COMSTR JSON_NOTIFY);

  for (i = 0, j = 0; i < total_devices; i++) {
    cgpu = get_devices(i);
    if (!opt_removedisabled || cgpu->deven != DEV_DISABLED)
      notifystatus(io_data, j++, cgpu, isjson, group);
  }

  if (isjson && io_open)
    io_close(io_data);
}

static void devdetails(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open = false;
  struct cgpu_info *cgpu;
  int i, j;

  if (total_devices == 0) {
    message(io_data, MSG_NODEVS, 0, NULL, isjson);
    return;
  }

  message(io_data, MSG_DEVDETAILS, 0, NULL, isjson);

  if (isjson)
    io_open = io_add(io_data, COMSTR JSON_DEVDETAILS);

  for (i = 0, j = 0; i < total_devices; i++) {
    cgpu = get_devices(i);

    if (opt_removedisabled && cgpu->deven == DEV_DISABLED) continue;

    root = api_add_int(root, "DEVDETAILS", &j, false);
    root = api_add_string(root, "Name", cgpu->drv->name, false);
    root = api_add_int(root, "ID", &(cgpu->device_id), false);
    root = api_add_string(root, "Driver", cgpu->drv->dname, false);
    root = api_add_const(root, "Kernel", cgpu->algorithm.name, false);
    root = api_add_const(root, "Model", cgpu->name ? cgpu->name : BLANK, false);
    root = api_add_const(root, "Device Path", cgpu->device_path ? cgpu->device_path : BLANK, false);

    root = print_data(root, buf, isjson, isjson && (j > 0));
    io_add(io_data, buf);
    j++;
  }

  if (isjson && io_open)
    io_close(io_data);
}

void dosave(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  char filename[PATH_MAX];
//  FILE *fcfg;
  char *ptr;

  if (param == NULL || *param == '\0') {
    default_save_file(filename);
    param = filename;
  }

  /*fcfg = fopen(param, "w");
  if (!fcfg) {
    ptr = escape_string(param, isjson);
    message(io_data, MSG_BADFN, 0, ptr, isjson);
    if (ptr != param)
      free(ptr);
    ptr = NULL;
    return;
  }*/

  write_config(param);
  //fclose(fcfg);

  ptr = escape_string(param, isjson);
  message(io_data, MSG_SAVED, 0, ptr, isjson);
  if (ptr != param)
    free(ptr);
  ptr = NULL;
}

static int itemstats(struct io_data *io_data, int i, char *id, struct sgminer_stats *stats, struct sgminer_pool_stats *pool_stats, struct api_data *extra, struct cgpu_info *cgpu, bool isjson)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];

  root = api_add_int(root, "STATS", &i, false);
  root = api_add_string(root, "ID", id, false);
  root = api_add_elapsed(root, "Elapsed", &(total_secs), false);
  root = api_add_uint32(root, "Calls", &(stats->getwork_calls), false);
  root = api_add_timeval(root, "Wait", &(stats->getwork_wait), false);
  root = api_add_timeval(root, "Max", &(stats->getwork_wait_max), false);
  root = api_add_timeval(root, "Min", &(stats->getwork_wait_min), false);

  if (pool_stats) {
    root = api_add_uint32(root, "Pool Calls", &(pool_stats->getwork_calls), false);
    root = api_add_uint32(root, "Pool Attempts", &(pool_stats->getwork_attempts), false);
    root = api_add_timeval(root, "Pool Wait", &(pool_stats->getwork_wait), false);
    root = api_add_timeval(root, "Pool Max", &(pool_stats->getwork_wait_max), false);
    root = api_add_timeval(root, "Pool Min", &(pool_stats->getwork_wait_min), false);
    root = api_add_double(root, "Pool Av", &(pool_stats->getwork_wait_rolling), false);
    root = api_add_bool(root, "Work Had Roll Time", &(pool_stats->hadrolltime), false);
    root = api_add_bool(root, "Work Can Roll", &(pool_stats->canroll), false);
    root = api_add_bool(root, "Work Had Expire", &(pool_stats->hadexpire), false);
    root = api_add_uint32(root, "Work Roll Time", &(pool_stats->rolltime), false);
    root = api_add_diff(root, "Work Diff", &(pool_stats->last_diff), false);
    root = api_add_diff(root, "Min Diff", &(pool_stats->min_diff), false);
    root = api_add_diff(root, "Max Diff", &(pool_stats->max_diff), false);
    root = api_add_uint32(root, "Min Diff Count", &(pool_stats->min_diff_count), false);
    root = api_add_uint32(root, "Max Diff Count", &(pool_stats->max_diff_count), false);
    root = api_add_uint64(root, "Times Sent", &(pool_stats->times_sent), false);
    root = api_add_uint64(root, "Bytes Sent", &(pool_stats->bytes_sent), false);
    root = api_add_uint64(root, "Times Recv", &(pool_stats->times_received), false);
    root = api_add_uint64(root, "Bytes Recv", &(pool_stats->bytes_received), false);
    root = api_add_uint64(root, "Net Bytes Sent", &(pool_stats->net_bytes_sent), false);
    root = api_add_uint64(root, "Net Bytes Recv", &(pool_stats->net_bytes_received), false);
  }

  if (extra)
    root = api_add_extra(root, extra);

  root = print_data(root, buf, isjson, isjson && (i > 0));
  io_add(io_data, buf);

  return ++i;
}

static void minerstats(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct cgpu_info *cgpu;
  bool io_open = false;
  struct api_data *extra;
  char id[20];
  int i, j;

  message(io_data, MSG_MINESTATS, 0, NULL, isjson);

  if (isjson)
    io_open = io_add(io_data, COMSTR JSON_MINESTATS);

  i = 0;
  for (j = 0; j < total_devices; j++) {
    cgpu = get_devices(j);

    if (cgpu && cgpu->drv && (!opt_removedisabled || cgpu->deven != DEV_DISABLED)) {
      if (cgpu->drv->get_api_stats)
        extra = cgpu->drv->get_api_stats(cgpu);
      else
        extra = NULL;

      sprintf(id, "%s%d", cgpu->drv->name, cgpu->device_id);
      i = itemstats(io_data, i, id, &(cgpu->sgminer_stats), NULL, extra, cgpu, isjson);
    }
  }

  for (j = 0; j < total_pools; j++) {
    struct pool *pool = pools[j];

    sprintf(id, "POOL%d", j);
    i = itemstats(io_data, i, id, &(pool->sgminer_stats), &(pool->sgminer_pool_stats), NULL, NULL, isjson);
  }

  if (isjson && io_open)
    io_close(io_data);
}

static void failoveronly(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISBOOL, 0, NULL, isjson);
    return;
  }

  *param = tolower(*param);

  if (*param != 't' && *param != 'f') {
    message(io_data, MSG_INVBOOL, 0, NULL, isjson);
    return;
  }

  bool tf = (*param == 't');

  opt_fail_only = tf;

  message(io_data, MSG_FOO, tf, NULL, isjson);
}

static void minecoin(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open;

  message(io_data, MSG_MINECOIN, 0, NULL, isjson);
  io_open = io_add(io_data, isjson ? COMSTR JSON_MINECOIN : _MINECOIN COMSTR);

  root = api_add_string(root, "Hash Method", get_devices(0)->algorithm.name, false);

  cg_rlock(&ch_lock);
  root = api_add_timeval(root, "Current Block Time", &block_timeval, true);
  root = api_add_string(root, "Current Block Hash", current_hash, true);
  cg_runlock(&ch_lock);

  root = api_add_bool(root, "LP", &have_longpoll, false);
  root = api_add_diff(root, "Network Difficulty", &current_diff, true);

  root = print_data(root, buf, isjson, false);
  io_add(io_data, buf);
  if (isjson && io_open)
    io_close(io_data);
}

static void debugstate(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open;

  if (param == NULL)
    param = (char *)BLANK;
  else
    *param = tolower(*param);

  switch(*param) {
  case 's':
    opt_realquiet = true;
    break;
  case 'q':
    opt_quiet ^= true;
    break;
  case 'v':
    opt_verbose ^= true;
    if (opt_verbose)
      opt_quiet = false;
    break;
  case 'd':
    opt_debug = true;
    opt_debug_console ^= true;
    opt_verbose = opt_debug_console;
    if (opt_debug_console)
      opt_quiet = false;
    break;
  case 'r':
    opt_protocol ^= true;
    if (opt_protocol)
      opt_quiet = false;
    break;
  case 'p':
    want_per_device_stats ^= true;
    opt_verbose = want_per_device_stats;
    break;
  case 'n':
    opt_verbose = false;
    opt_debug_console = false;
    opt_quiet = false;
    opt_protocol = false;
    want_per_device_stats = false;
    opt_worktime = false;
    break;
  case 'w':
    opt_worktime ^= true;
    break;
#ifdef _MEMORY_DEBUG
  case 'y':
    cgmemspeedup();
    break;
  case 'z':
    cgmemrpt();
    break;
#endif
  default:
    // anything else just reports the settings
    break;
  }

  message(io_data, MSG_DEBUGSET, 0, NULL, isjson);
  io_open = io_add(io_data, isjson ? COMSTR JSON_DEBUGSET : _DEBUGSET COMSTR);

  root = api_add_bool(root, "Silent", &opt_realquiet, false);
  root = api_add_bool(root, "Quiet", &opt_quiet, false);
  root = api_add_bool(root, "Verbose", &opt_verbose, false);
  root = api_add_bool(root, "Debug", &opt_debug_console, false);
  root = api_add_bool(root, "RPCProto", &opt_protocol, false);
  root = api_add_bool(root, "PerDevice", &want_per_device_stats, false);
  root = api_add_bool(root, "WorkTime", &opt_worktime, false);

  root = print_data(root, buf, isjson, false);
  io_add(io_data, buf);
  if (isjson && io_open)
    io_close(io_data);
}

static void setconfig(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  char *comma;
  int value;

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_CONPAR, 0, NULL, isjson);
    return;
  }

  comma = strchr(param, ',');
  if (!comma) {
    message(io_data, MSG_CONVAL, 0, param, isjson);
    return;
  }

  *(comma++) = '\0';
  value = atoi(comma);
  if (value < 0 || value > 9999) {
    message(io_data, MSG_INVNUM, value, param, isjson);
    return;
  }

  if (strcasecmp(param, "queue") == 0)
    opt_queue = value;
  else if (strcasecmp(param, "scantime") == 0)
    opt_scantime = value;
  else if (strcasecmp(param, "expiry") == 0)
    opt_expiry = value;
  else {
    message(io_data, MSG_UNKCON, 0, param, isjson);
    return;
  }

  message(io_data, MSG_SETCONFIG, value, param, isjson);
}

static void dozero(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
  if (param == NULL || *param == '\0') {
    message(io_data, MSG_ZERMIS, 0, NULL, isjson);
    return;
  }

  char *sum = strchr(param, ',');
  if (sum)
    *(sum++) = '\0';
  if (!sum || !*sum) {
    message(io_data, MSG_MISBOOL, 0, NULL, isjson);
    return;
  }

  bool all = false;
  bool bs = false;
  if (strcasecmp(param, "all") == 0)
    all = true;
  else if (strcasecmp(param, "bestshare") == 0)
    bs = true;

  if (all == false && bs == false) {
    message(io_data, MSG_ZERINV, 0, param, isjson);
    return;
  }

  *sum = tolower(*sum);
  if (*sum != 't' && *sum != 'f') {
    message(io_data, MSG_INVBOOL, 0, NULL, isjson);
    return;
  }

  bool dosum = (*sum == 't');
  if (dosum)
    print_summary();

  if (all)
    zero_stats();
  if (bs)
    zero_bestshare();

  if (dosum)
    message(io_data, MSG_ZERSUM, 0, all ? "All" : "BestShare", isjson);
  else
    message(io_data, MSG_ZERNOSUM, 0, all ? "All" : "BestShare", isjson);
}

static void checkcommand(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, char group);

struct CMDS {
  char *name;
  void (*func)(struct io_data *, SOCKETTYPE, char *, bool, char);
  bool iswritemode;
  bool joinable;
} cmds[] = {
  { "version",    apiversion, false,  true },
  { "config",   minerconfig,  false,  true },
  { "devs",   devstatus,  false,  true },
  { "pools",    poolstatus, false,  true },
  { "profiles",    api_profile_list, false,  true },
  { "summary",    summary,  false,  true },
  { "gpuenable",          gpuenable,      true, false },
  { "gpudisable",         gpudisable,     true, false },
  { "gpurestart",         gpurestart,     true, false },
  { "gpu",                gpudev,         false,  false },
  { "gpucount",           gpucount,       false,  true },
  { "switchpool",   switchpool, true, false },
  { "changestrategy",   api_pool_strategy, true, false },
  { "addpool",    addpool,  true, false },
  { "poolpriority", poolpriority, true, false },
  { "poolquota",    poolquota,  true, false },
  { "enablepool",   enablepool, true, false },
  { "disablepool",  disablepool,  true, false },
  { "removepool",   removepool, true, false },
  { "changepoolprofile",   api_pool_profile, true, false },
  { "addprofile",    api_profile_add,  true, false },
  { "removeprofile",    api_profile_remove,  true, false },
  { "gpuintensity",       gpuintensity,   true, false },
  { "gpuxintensity",       gpuxintensity,   true, false },
  { "gpurawintensity",       gpurawintensity,   true, false },
  { "gpumem",             gpumem,         true, false },
  { "gpuengine",          gpuengine,      true, false },
  { "gpufan",             gpufan,         true, false },
  { "gpuvddc",            gpuvddc,        true, false },
  { "save",   dosave,   true, false },
  { "quit",   doquit,   true, false },
  { "privileged",   privileged, true, false },
  { "notify",   notify,   false,  true },
  { "devdetails",   devdetails, false,  true },
  { "restart",    dorestart,  true, false },
  { "stats",    minerstats, false,  true },
  { "check",    checkcommand, false,  false },
  { "failover-only",  failoveronly, true, false },
  { "coin",   minecoin, false,  true },
  { "debug",    debugstate, true, false },
  { "setconfig",    setconfig,  true, false },
  { "zero",   dozero,   true, false },
  { "lockstats",    lockstats,  true, true },
  { NULL,     NULL,   false,  false }
};

static void checkcommand(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, char group)
{
  struct api_data *root = NULL;
  char buf[TMPBUFSIZ];
  bool io_open;
  char cmdbuf[100];
  bool found, access;
  int i;

  if (param == NULL || *param == '\0') {
    message(io_data, MSG_MISCHK, 0, NULL, isjson);
    return;
  }

  found = false;
  access = false;
  for (i = 0; cmds[i].name != NULL; i++) {
    if (strcmp(cmds[i].name, param) == 0) {
      found = true;

      sprintf(cmdbuf, "|%s|", param);
      if (ISPRIVGROUP(group) || strstr(COMMANDS(group), cmdbuf))
        access = true;

      break;
    }
  }

  message(io_data, MSG_CHECK, 0, NULL, isjson);
  io_open = io_add(io_data, isjson ? COMSTR JSON_CHECK : _CHECK COMSTR);

  root = api_add_const(root, "Exists", found ? YES : NO, false);
  root = api_add_const(root, "Access", access ? YES : NO, false);

  root = print_data(root, buf, isjson, false);
  io_add(io_data, buf);
  if (isjson && io_open)
    io_close(io_data);
}

static void head_join(struct io_data *io_data, char *cmdptr, bool isjson, bool *firstjoin)
{
  char *ptr;

  if (*firstjoin) {
    if (isjson)
      io_add(io_data, JSON0);
    *firstjoin = false;
  } else {
    if (isjson)
      io_add(io_data, JSON_BETWEEN_JOIN);
  }

  // External supplied string
  ptr = escape_string(cmdptr, isjson);

  if (isjson) {
    io_add(io_data, JSON1);
    io_add(io_data, ptr);
    io_add(io_data, JSON2);
  } else {
    io_add(io_data, JOIN_CMD);
    io_add(io_data, ptr);
    io_add(io_data, BETWEEN_JOIN);
  }

  if (ptr != cmdptr)
    free(ptr);
}

static void tail_join(struct io_data *io_data, bool isjson)
{
  if (io_data->close) {
    io_add(io_data, JSON_CLOSE);
    io_data->close = false;
  }

  if (isjson) {
    io_add(io_data, JSON_END);
    io_add(io_data, JSON3);
  }
}

static void send_result(struct io_data *io_data, SOCKETTYPE c, bool isjson)
{
  int count, sendc, res, tosend, len, n;
  char *buf = io_data->ptr;

  strcpy(buf, io_data->ptr);

  if (io_data->close)
    strcat(buf, JSON_CLOSE);

  if (isjson)
    strcat(buf, JSON_END);

  len = strlen(buf);
  tosend = len+1;

  applog(LOG_DEBUG, "API: send reply: (%d) '%.10s%s'", tosend, buf, len > 10 ? "..." : BLANK);

  count = sendc = 0;
  while (count < 5 && tosend > 0) {
    // allow 50ms per attempt
    struct timeval timeout = {0, 50000};
    fd_set wd;

    FD_ZERO(&wd);
    FD_SET(c, &wd);
    if ((res = select(c + 1, NULL, &wd, NULL, &timeout)) < 1) {
      applog(LOG_WARNING, "API: send select failed (%d)", res);
      return;
    }

    n = send(c, buf, tosend, 0);
    sendc++;

    if (SOCKETFAIL(n)) {
      count++;
      if (sock_blocks())
        continue;

      applog(LOG_WARNING, "API: send (%d:%d) failed: %s", len+1, (len+1 - tosend), SOCKERRMSG);

      return;
    } else {
      if (sendc <= 1) {
        if (n == tosend)
          applog(LOG_DEBUG, "API: sent all of %d first go", tosend);
        else
          applog(LOG_DEBUG, "API: sent %d of %d first go", n, tosend);
      } else {
        if (n == tosend)
          applog(LOG_DEBUG, "API: sent all of remaining %d (sendc=%d)", tosend, sendc);
        else
          applog(LOG_DEBUG, "API: sent %d of remaining %d (sendc=%d)", n, tosend, sendc);
      }

      tosend -= n;
      buf += n;

      if (n == 0)
        count++;
    }
  }
}

static void tidyup(__maybe_unused void *arg)
{
  mutex_lock(&quit_restart_lock);

  SOCKETTYPE *apisock = (SOCKETTYPE *)arg;

  bye = true;

  if (*apisock != INVSOCK) {
    shutdown(*apisock, SHUT_RDWR);
    CLOSESOCKET(*apisock);
    *apisock = INVSOCK;
  }

  if (ipaccess != NULL) {
    free(ipaccess);
    ipaccess = NULL;
  }

  io_free();

  mutex_unlock(&quit_restart_lock);
}

/*
 * Interpret --api-groups G:cmd1:cmd2:cmd3,P:cmd4,*,...
 */
static void setup_groups()
{
  char *api_groups = opt_api_groups ? opt_api_groups : (char *)BLANK;
  char *buf, *ptr, *next, *colon;
  char group;
  char commands[TMPBUFSIZ];
  char cmdbuf[100];
  char *cmd;
  bool addstar, did;
  int i;

  buf = (char *)malloc(strlen(api_groups) + 1);
  if (unlikely(!buf))
    quit(1, "Failed to malloc ipgroups buf");

  strcpy(buf, api_groups);

  next = buf;
  // for each group defined
  while (next && *next) {
    ptr = next;
    next = strchr(ptr, ',');
    if (next)
      *(next++) = '\0';

    // Validate the group
    if (*(ptr+1) != ':') {
      colon = strchr(ptr, ':');
      if (colon)
        *colon = '\0';
      quit(1, "API invalid group name '%s'", ptr);
    }

    group = GROUP(*ptr);
    if (!VALIDGROUP(group))
      quit(1, "API invalid group name '%c'", *ptr);

    if (group == PRIVGROUP)
      quit(1, "API group name can't be '%c'", PRIVGROUP);

    if (group == NOPRIVGROUP)
      quit(1, "API group name can't be '%c'", NOPRIVGROUP);

    if (apigroups[GROUPOFFSET(group)].commands != NULL)
      quit(1, "API duplicate group name '%c'", *ptr);

    ptr += 2;

    // Validate the command list (and handle '*')
    cmd = &(commands[0]);
    *(cmd++) = SEPARATOR;
    *cmd = '\0';
    addstar = false;
    while (ptr && *ptr) {
      colon = strchr(ptr, ':');
      if (colon)
        *(colon++) = '\0';

      if (strcmp(ptr, "*") == 0)
        addstar = true;
      else {
        did = false;
        for (i = 0; cmds[i].name != NULL; i++) {
          if (strcasecmp(ptr, cmds[i].name) == 0) {
            did = true;
            break;
          }
        }
        if (did) {
          // skip duplicates
          sprintf(cmdbuf, "|%s|", cmds[i].name);
          if (strstr(commands, cmdbuf) == NULL) {
            strcpy(cmd, cmds[i].name);
            cmd += strlen(cmds[i].name);
            *(cmd++) = SEPARATOR;
            *cmd = '\0';
          }
        } else {
          quit(1, "API unknown command '%s' in group '%c'", ptr, group);
        }
      }

      ptr = colon;
    }

    // * = allow all non-iswritemode commands
    if (addstar) {
      for (i = 0; cmds[i].name != NULL; i++) {
        if (cmds[i].iswritemode == false) {
          // skip duplicates
          sprintf(cmdbuf, "|%s|", cmds[i].name);
          if (strstr(commands, cmdbuf) == NULL) {
            strcpy(cmd, cmds[i].name);
            cmd += strlen(cmds[i].name);
            *(cmd++) = SEPARATOR;
            *cmd = '\0';
          }
        }
      }
    }

    ptr = apigroups[GROUPOFFSET(group)].commands = (char *)malloc(strlen(commands) + 1);
    if (unlikely(!ptr))
      quit(1, "Failed to malloc group commands buf");

    strcpy(ptr, commands);
  }

  // Now define R (NOPRIVGROUP) as all non-iswritemode commands
  cmd = &(commands[0]);
  *(cmd++) = SEPARATOR;
  *cmd = '\0';
  for (i = 0; cmds[i].name != NULL; i++) {
    if (cmds[i].iswritemode == false) {
      strcpy(cmd, cmds[i].name);
      cmd += strlen(cmds[i].name);
      *(cmd++) = SEPARATOR;
      *cmd = '\0';
    }
  }

  ptr = apigroups[GROUPOFFSET(NOPRIVGROUP)].commands = (char *)malloc(strlen(commands) + 1);
  if (unlikely(!ptr))
    quit(1, "Failed to malloc noprivgroup commands buf");

  strcpy(ptr, commands);

  // W (PRIVGROUP) is handled as a special case since it simply means all commands

  free(buf);
  return;
}

/*
 * Interpret [W:]IP[/Prefix][,[R|W:]IP2[/Prefix2][,...]] --api-allow option
 *  special case of 0/0 allows /0 (means all IP addresses)
 */
#define ALLIP4 "0/0"
/*
 * N.B. IP4 addresses are by Definition 32bit big endian on all platforms
 */
static void setup_ipaccess()
{
  char *buf, *ptr, *comma, *slash, *dot;
  int ipcount, mask, octet, i;
  char group;

  buf = (char *)malloc(strlen(opt_api_allow) + 1);
  if (unlikely(!buf))
    quit(1, "Failed to malloc ipaccess buf");

  strcpy(buf, opt_api_allow);

  ipcount = 1;
  ptr = buf;
  while (*ptr)
    if (*(ptr++) == ',')
      ipcount++;

  // possibly more than needed, but never less
  ipaccess = (struct IP4ACCESS *)calloc(ipcount, sizeof(struct IP4ACCESS));
  if (unlikely(!ipaccess))
    quit(1, "Failed to calloc ipaccess");

  ips = 0;
  ptr = buf;
  while (ptr && *ptr) {
    while (*ptr == ' ' || *ptr == '\t')
      ptr++;

    if (*ptr == ',') {
      ptr++;
      continue;
    }

    comma = strchr(ptr, ',');
    if (comma)
      *(comma++) = '\0';

    group = NOPRIVGROUP;

    if (isalpha(*ptr) && *(ptr+1) == ':') {
      if (DEFINEDGROUP(*ptr))
        group = GROUP(*ptr);

      ptr += 2;
    }

    ipaccess[ips].group = group;

    if (strcmp(ptr, ALLIP4) == 0)
      ipaccess[ips].ip = ipaccess[ips].mask = 0;
    else {
      slash = strchr(ptr, '/');
      if (!slash)
        ipaccess[ips].mask = 0xffffffff;
      else {
        *(slash++) = '\0';
        mask = atoi(slash);
        if (mask < 1 || mask > 32)
          goto popipo; // skip invalid/zero

        ipaccess[ips].mask = 0;
        while (mask-- >= 0) {
          octet = 1 << (mask % 8);
          ipaccess[ips].mask |= (octet << (24 - (8 * (mask >> 3))));
        }
      }

      ipaccess[ips].ip = 0; // missing default to '.0'
      for (i = 0; ptr && (i < 4); i++) {
        dot = strchr(ptr, '.');
        if (dot)
          *(dot++) = '\0';

        octet = atoi(ptr);
        if (octet < 0 || octet > 0xff)
          goto popipo; // skip invalid

        ipaccess[ips].ip |= (octet << (24 - (i * 8)));

        ptr = dot;
      }

      ipaccess[ips].ip &= ipaccess[ips].mask;
    }

    ips++;
popipo:
    ptr = comma;
  }

  free(buf);
}

static void *quit_thread(__maybe_unused void *userdata)
{
  // allow thread creator to finish whatever it's doing
  mutex_lock(&quit_restart_lock);
  mutex_unlock(&quit_restart_lock);

  applog(LOG_DEBUG, "API: killing sgminer");

  kill_work();

  return NULL;
}

static void *restart_thread(__maybe_unused void *userdata)
{
  // allow thread creator to finish whatever it's doing
  mutex_lock(&quit_restart_lock);
  mutex_unlock(&quit_restart_lock);

  applog(LOG_DEBUG, "API: restarting sgminer");

  app_restart();

  return NULL;
}

static bool check_connect(struct sockaddr_in *cli, char **connectaddr, char *group)
{
  bool addrok = false;
  int i;

  *connectaddr = inet_ntoa(cli->sin_addr);

  *group = NOPRIVGROUP;
  if (opt_api_allow) {
    int client_ip = htonl(cli->sin_addr.s_addr);
    for (i = 0; i < ips; i++) {
      if ((client_ip & ipaccess[i].mask) == ipaccess[i].ip) {
        addrok = true;
        *group = ipaccess[i].group;
        break;
      }
    }
  } else {
    if (opt_api_network)
      addrok = true;
    else
      addrok = (strcmp(*connectaddr, localaddr) == 0);
  }

  return addrok;
}

static void mcast()
{
  struct sockaddr_in listen;
  struct ip_mreq grp;
  struct sockaddr_in came_from;
  time_t bindstart;
  char *binderror;
  SOCKETTYPE mcast_sock;
  SOCKETTYPE reply_sock;
  socklen_t came_from_siz;
  char *connectaddr;
  ssize_t rep;
  int bound;
  int count;
  int reply_port;
  bool addrok;
  char group;

  char expect[] = "sgminer-"; // first 8 bytes constant
  char *expect_code;
  size_t expect_code_len;
  char buf[1024];
  char replybuf[1024];

  memset(&grp, 0, sizeof(grp));
  grp.imr_multiaddr.s_addr = inet_addr(opt_api_mcast_addr);
  if (grp.imr_multiaddr.s_addr == INADDR_NONE)
    quit(1, "Invalid Multicast Address");
  grp.imr_interface.s_addr = INADDR_ANY;

  mcast_sock = socket(AF_INET, SOCK_DGRAM, 0);

  int optval = 1;
  if (SOCKETFAIL(setsockopt(mcast_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)(&optval), sizeof(optval)))) {
    applog(LOG_ERR, "API mcast setsockopt SO_REUSEADDR failed (%s)%s", SOCKERRMSG, MUNAVAILABLE);
    goto die;
  }

  memset(&listen, 0, sizeof(listen));
  listen.sin_family = AF_INET;
  listen.sin_addr.s_addr = INADDR_ANY;
  listen.sin_port = htons(opt_api_mcast_port);

  // try for more than 1 minute ... in case the old one hasn't completely gone yet
  bound = 0;
  bindstart = time(NULL);
  while (bound == 0) {
    if (SOCKETFAIL(bind(mcast_sock, (struct sockaddr *)(&listen), sizeof(listen)))) {
      binderror = SOCKERRMSG;
      if ((time(NULL) - bindstart) > 61)
        break;
      else
        cgsleep_ms(30000);
    } else
      bound = 1;
  }

  if (bound == 0) {
    applog(LOG_ERR, "API mcast bind to port %d failed (%s)%s", opt_api_port, binderror, MUNAVAILABLE);
    goto die;
  }

  if (SOCKETFAIL(setsockopt(mcast_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)(&grp), sizeof(grp)))) {
    applog(LOG_ERR, "API mcast join failed (%s)%s", SOCKERRMSG, MUNAVAILABLE);
    goto die;
  }

  expect_code_len = sizeof(expect) + strlen(opt_api_mcast_code);
  expect_code = (char *)malloc(expect_code_len + 1);
  if (!expect_code)
    quit(1, "Failed to malloc mcast expect_code");
  snprintf(expect_code, expect_code_len+1, "%s%s-", expect, opt_api_mcast_code);

  count = 0;
  while (80085) {
    cgsleep_ms(1000);

    count++;
    came_from_siz = sizeof(came_from);
    if (SOCKETFAIL(rep = recvfrom(mcast_sock, buf, sizeof(buf) - 1,
            0, (struct sockaddr *)(&came_from), &came_from_siz))) {
      applog(LOG_DEBUG, "API mcast failed count=%d (%s) (%d)",
          count, SOCKERRMSG, (int)mcast_sock);
      continue;
    }

    addrok = check_connect(&came_from, &connectaddr, &group);
    applog(LOG_DEBUG, "API mcast from %s - %s",
          connectaddr, addrok ? "Accepted" : "Ignored");
    if (!addrok)
      continue;

    buf[rep] = '\0';
    if (rep > 0 && buf[rep-1] == '\n')
      buf[--rep] = '\0';

    applog(LOG_DEBUG, "API mcast request rep=%d (%s) from %s:%d",
          (int)rep, buf,
          inet_ntoa(came_from.sin_addr),
          ntohs(came_from.sin_port));

    if ((size_t)rep > expect_code_len && memcmp(buf, expect_code, expect_code_len) == 0) {
      reply_port = atoi(&buf[expect_code_len]);
      if (reply_port < 1 || reply_port > 65535) {
        applog(LOG_DEBUG, "API mcast request ignored - invalid port (%s)",
              &buf[expect_code_len]);
      } else {
        applog(LOG_DEBUG, "API mcast request OK port %s=%d",
              &buf[expect_code_len], reply_port);

        came_from.sin_port = htons(reply_port);
        reply_sock = socket(AF_INET, SOCK_DGRAM, 0);

        snprintf(replybuf, sizeof(replybuf),
              "cgm-" API_MCAST_CODE "-%d-%s",
              opt_api_port, opt_api_mcast_des);

        rep = sendto(reply_sock, replybuf, strlen(replybuf)+1,
            0, (struct sockaddr *)(&came_from),
            sizeof(came_from));
        if (SOCKETFAIL(rep)) {
          applog(LOG_DEBUG, "API mcast send reply failed (%s) (%d)",
                SOCKERRMSG, (int)reply_sock);
        } else {
          applog(LOG_DEBUG, "API mcast send reply (%s) succeeded (%d) (%d)",
                replybuf, (int)rep, (int)reply_sock);
        }

        CLOSESOCKET(reply_sock);
      }
    } else
      applog(LOG_DEBUG, "API mcast request was no good");
  }

die:

  CLOSESOCKET(mcast_sock);
}

static void *mcast_thread(void *userdata)
{
  struct thr_info *mythr = (struct thr_info *)userdata;

  pthread_detach(pthread_self());
  /*pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);wo gai de */

  RenameThread("APIMcast");

  mcast();

  PTH(mythr) = 0L;

  return NULL;
}

void mcast_init()
{
  struct thr_info *thr;

  thr = (struct thr_info *)calloc(1, sizeof(*thr));
  if (!thr)
    quit(1, "Failed to calloc mcast thr");

  if (thr_info_create(thr, NULL, mcast_thread, thr))
    quit(1, "API mcast thread create failed");
}

void api(int api_thr_id)
{
  struct io_data *io_data;
  struct thr_info bye_thr;
  char buf[TMPBUFSIZ];
  char param_buf[TMPBUFSIZ];
  SOCKETTYPE c;
  int n, bound;
  char *connectaddr;
  char *binderror;
  time_t bindstart;
  short int port = opt_api_port;
  struct sockaddr_in serv;
  struct sockaddr_in cli;
  socklen_t clisiz;
  char cmdbuf[100];
  char *cmd = NULL, *cmdptr, *cmdsbuf = NULL;
  char *param;
  bool addrok;
  char group;
  json_error_t json_err;
  json_t *json_config = NULL;
  json_t *json_val;
  bool isjson;
  bool did, isjoin = false, firstjoin;
  int i;

  SOCKETTYPE *apisock;

  apisock = (SOCKETTYPE *)malloc(sizeof(*apisock));
  *apisock = INVSOCK;

  if (!opt_api_listen) {
    applog(LOG_DEBUG, "API not running%s", UNAVAILABLE);
    free(apisock);
    return;
  }

  io_data = sock_io_new();

  mutex_init(&quit_restart_lock);

  pthread_cleanup_push(tidyup, (void *)apisock);
  my_thr_id = api_thr_id;

  setup_groups();

  if (opt_api_allow) {
    setup_ipaccess();

    if (ips == 0) {
      applog(LOG_WARNING, "API not running (no valid IPs specified)%s", UNAVAILABLE);
      free(apisock);
      return;
    }
  }

  /* This should be done before curl in needed
   * to ensure curl has already called WSAStartup() in windows */
  cgsleep_ms(opt_log_interval*1000);

  *apisock = socket(AF_INET, SOCK_STREAM, 0);
  if (*apisock == INVSOCK) {
    applog(LOG_ERR, "API1 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
    free(apisock);
    return;
  }

  memset(&serv, 0, sizeof(serv));

  serv.sin_family = AF_INET;

  if (!opt_api_allow && !opt_api_network) {
    serv.sin_addr.s_addr = inet_addr(localaddr);
    if (serv.sin_addr.s_addr == (in_addr_t)INVINETADDR) {
      applog(LOG_ERR, "API2 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
      free(apisock);
      return;
    }
  }

  serv.sin_port = htons(port);

#ifndef WIN32
  // On linux with SO_REUSEADDR, bind will get the port if the previous
  // socket is closed (even if it is still in TIME_WAIT) but fail if
  // another program has it open - which is what we want
  int optval = 1;
  // If it doesn't work, we don't really care - just show a debug message
  if (SOCKETFAIL(setsockopt(*apisock, SOL_SOCKET, SO_REUSEADDR, (void *)(&optval), sizeof(optval))))
    applog(LOG_DEBUG, "API setsockopt SO_REUSEADDR failed (ignored): %s", SOCKERRMSG);
#else
  // On windows a 2nd program can bind to a port>1024 already in use unless
  // SO_EXCLUSIVEADDRUSE is used - however then the bind to a closed port
  // in TIME_WAIT will fail until the timeout - so we leave the options alone
#endif

  // try for more than 1 minute ... in case the old one hasn't completely gone yet
  bound = 0;
  bindstart = time(NULL);
  while (bound == 0) {
    if (SOCKETFAIL(bind(*apisock, (struct sockaddr *)(&serv), sizeof(serv)))) {
      binderror = SOCKERRMSG;
      if ((time(NULL) - bindstart) > 61)
        break;
      else {
        applog(LOG_WARNING, "API bind to port %d failed - trying again in 30sec", port);
        cgsleep_ms(30000);
      }
    } else
      bound = 1;
  }

  if (bound == 0) {
    applog(LOG_ERR, "API bind to port %d failed (%s)%s", port, binderror, UNAVAILABLE);
    free(apisock);
    return;
  }

  if (SOCKETFAIL(listen(*apisock, QUEUE))) {
    applog(LOG_ERR, "API3 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
    CLOSESOCKET(*apisock);
    free(apisock);
    return;
  }

  if (opt_api_allow)
    applog(LOG_WARNING, "API running in IP access mode on port %d (%d)", port, (int)*apisock);
  else {
    if (opt_api_network)
      applog(LOG_WARNING, "API running in UNRESTRICTED read access mode on port %d (%d)", port, (int)*apisock);
    else
      applog(LOG_WARNING, "API running in local read access mode on port %d (%d)", port, (int)*apisock);
  }

  if (opt_api_mcast)
    mcast_init();

  while (!bye) {
    clisiz = sizeof(cli);
    if (SOCKETFAIL(c = accept(*apisock, (struct sockaddr *)(&cli), &clisiz))) {
      applog(LOG_ERR, "API failed (%s)%s (%d)", SOCKERRMSG, UNAVAILABLE, (int)*apisock);
      goto die;
    }

    addrok = check_connect(&cli, &connectaddr, &group);
    applog(LOG_DEBUG, "API: connection from %s - %s",
          connectaddr, addrok ? "Accepted" : "Ignored");

    if (addrok) {
      n = recv(c, &buf[0], TMPBUFSIZ-1, 0);
      if (SOCKETFAIL(n))
        buf[0] = '\0';
      else
        buf[n] = '\0';

      if (SOCKETFAIL(n))
        applog(LOG_DEBUG, "API: recv failed: %s", SOCKERRMSG);
      else
        applog(LOG_DEBUG, "API: recv command: (%d) '%s'", n, buf);

      if (!SOCKETFAIL(n)) {
        // the time of the request in now
        when = time(NULL);
        io_reinit(io_data);

        did = false;

        if (*buf != ISJSON) {
          isjson = false;

          param = strchr(buf, SEPARATOR);
          if (param != NULL)
            *(param++) = '\0';

          cmd = buf;
        }
        else {
          isjson = true;

          param = NULL;

#if JANSSON_MAJOR_VERSION > 2 || (JANSSON_MAJOR_VERSION == 2 && JANSSON_MINOR_VERSION > 0)
          json_config = json_loadb(buf, n, 0, &json_err);
#elif JANSSON_MAJOR_VERSION > 1
          json_config = json_loads(buf, 0, &json_err);
#else
          json_config = json_loads(buf, &json_err);
#endif

          if (!json_is_object(json_config)) {
            message(io_data, MSG_INVJSON, 0, NULL, isjson);
            send_result(io_data, c, isjson);
            did = true;
          } else {
            json_val = json_object_get(json_config, JSON_COMMAND);
            if (json_val == NULL) {
              message(io_data, MSG_MISCMD, 0, NULL, isjson);
              send_result(io_data, c, isjson);
              did = true;
            } else {
              if (!json_is_string(json_val)) {
                message(io_data, MSG_INVCMD, 0, NULL, isjson);
                send_result(io_data, c, isjson);
                did = true;
              } else {
                cmd = (char *)json_string_value(json_val);
                json_val = json_object_get(json_config, JSON_PARAMETER);
                if (json_is_string(json_val))
                  param = (char *)json_string_value(json_val);
                else if (json_is_integer(json_val)) {
                  sprintf(param_buf, "%d", (int)json_integer_value(json_val));
                  param = param_buf;
                } else if (json_is_real(json_val)) {
                  sprintf(param_buf, "%f", (double)json_real_value(json_val));
                  param = param_buf;
                }
              }
            }
          }
        }

        if (!did) {
          if (strchr(cmd, CMDJOIN)) {
            firstjoin = isjoin = true;
            // cmd + leading '|' + '\0'
            cmdsbuf = (char *)malloc(strlen(cmd) + 2);
            if (!cmdsbuf)
              quithere(1, "OOM cmdsbuf");
            strcpy(cmdsbuf, "|");
            param = NULL;
          } else
            firstjoin = isjoin = false;

          cmdptr = cmd;
          do {
            did = false;
            if (isjoin) {
              cmd = strchr(cmdptr, CMDJOIN);
              if (cmd)
                *(cmd++) = '\0';
              if (!*cmdptr)
                goto inochi;
            }

            for (i = 0; cmds[i].name != NULL; i++) {
              if (strcmp(cmdptr, cmds[i].name) == 0) {
                sprintf(cmdbuf, "|%s|", cmdptr);
                if (isjoin) {
                  if (strstr(cmdsbuf, cmdbuf)) {
                    did = true;
                    break;
                  }
                  strcat(cmdsbuf, cmdptr);
                  strcat(cmdsbuf, "|");
                  head_join(io_data, cmdptr, isjson, &firstjoin);
                  if (!cmds[i].joinable) {
                    message(io_data, MSG_ACCDENY, 0, cmds[i].name, isjson);
                    did = true;
                    tail_join(io_data, isjson);
                    break;
                  }
                }
                if (ISPRIVGROUP(group) || strstr(COMMANDS(group), cmdbuf))
                  (cmds[i].func)(io_data, c, param, isjson, group);
                else {
                  message(io_data, MSG_ACCDENY, 0, cmds[i].name, isjson);
                  applog(LOG_DEBUG, "API: access denied to '%s' for '%s' command", connectaddr, cmds[i].name);
                }

                did = true;
                if (!isjoin)
                  send_result(io_data, c, isjson);
                else
                  tail_join(io_data, isjson);
                break;
              }
            }

            if (!did) {
              if (isjoin)
                head_join(io_data, cmdptr, isjson, &firstjoin);
              message(io_data, MSG_INVCMD, 0, NULL, isjson);
              if (isjoin)
                tail_join(io_data, isjson);
              else
                send_result(io_data, c, isjson);
            }
inochi:
            if (isjoin)
              cmdptr = cmd;
          } while (isjoin && cmdptr);
        }

        if (isjoin)
          send_result(io_data, c, isjson);

        if (isjson && json_is_object(json_config))
          json_decref(json_config);
      }
    }
    CLOSESOCKET(c);
  }
die:
  /* Blank line fix for older compilers since pthread_cleanup_pop is a
   * macro that gets confused by a label existing immediately before it
   */
  ;
  pthread_cleanup_pop(true);

  free(apisock);

  applog(LOG_DEBUG, "API: terminating due to: %s",
      do_a_quit ? "QUIT" : (do_a_restart ? "RESTART" : (bye ? "BYE" : "UNKNOWN!")));

  mutex_lock(&quit_restart_lock);

  if (do_a_restart) {
    if (thr_info_create(&bye_thr, NULL, restart_thread, &bye_thr)) {
      mutex_unlock(&quit_restart_lock);
      quit(1, "API failed to initiate a restart - aborting");
    }
    pthread_detach(bye_thr.pth);
  } else if (do_a_quit) {
    if (thr_info_create(&bye_thr, NULL, quit_thread, &bye_thr)) {
      mutex_unlock(&quit_restart_lock);
      quit(1, "API failed to initiate a clean quit - aborting");
    }
    pthread_detach(bye_thr.pth);
  }

  mutex_unlock(&quit_restart_lock);
}
