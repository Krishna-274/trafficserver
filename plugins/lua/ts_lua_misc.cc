/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <netinet/in.h>
#include <arpa/inet.h>
#include "ts_lua_util.h"

static int ts_lua_get_process_id(lua_State *L);
static int ts_lua_get_now_time(lua_State *L);
static int ts_lua_debug(lua_State *L);
static int ts_lua_is_debug_tag_set(lua_State *L);
static int ts_lua_error(lua_State *L);
static int ts_lua_emergency(lua_State *L);
static int ts_lua_fatal(lua_State *L);
static int ts_lua_status(lua_State *L);
static int ts_lua_note(lua_State *L);
static int ts_lua_warning(lua_State *L);
static int ts_lua_alert(lua_State *L);
static int ts_lua_sleep(lua_State *L);
static int ts_lua_host_lookup(lua_State *L);
static int ts_lua_schedule(lua_State *L);
static int ts_lua_get_install_dir(lua_State *L);
static int ts_lua_get_config_dir(lua_State *L);
static int ts_lua_get_runtime_dir(lua_State *L);
static int ts_lua_get_plugin_dir(lua_State *L);
static int ts_lua_get_traffic_server_version(lua_State *L);

static int ts_lua_sleep_cleanup(ts_lua_async_item *ai);
static int ts_lua_sleep_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_host_lookup_cleanup(ts_lua_async_item *ai);
static int ts_lua_host_lookup_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_schedule_handler(TSCont contp, TSEvent event, void *edata);

static void ts_lua_inject_misc_variables(lua_State *L);

void
ts_lua_inject_misc_api(lua_State *L)
{
  lua_newtable(L);

  /* ts.process.uuid() */
  lua_pushcfunction(L, ts_lua_get_process_id);
  lua_setfield(L, -2, "uuid");

  lua_setfield(L, -2, "process");

  /* ts.now() */
  lua_pushcfunction(L, ts_lua_get_now_time);
  lua_setfield(L, -2, "now");

  /* ts.debug(...) */
  lua_pushcfunction(L, ts_lua_debug);
  lua_setfield(L, -2, "debug");

  /* ts.is_debug_tag_set(...) */
  lua_pushcfunction(L, ts_lua_is_debug_tag_set);
  lua_setfield(L, -2, "is_debug_tag_set");

  /* ts.error(...) */
  lua_pushcfunction(L, ts_lua_error);
  lua_setfield(L, -2, "error");

  /* ts.emergency(...) */
  lua_pushcfunction(L, ts_lua_emergency);
  lua_setfield(L, -2, "emergency");

  /* ts.fatal(...) */
  lua_pushcfunction(L, ts_lua_fatal);
  lua_setfield(L, -2, "fatal");

  /* ts.status(...) */
  lua_pushcfunction(L, ts_lua_status);
  lua_setfield(L, -2, "status");

  /* ts.note(...) */
  lua_pushcfunction(L, ts_lua_note);
  lua_setfield(L, -2, "note");

  /* ts.warning(...) */
  lua_pushcfunction(L, ts_lua_warning);
  lua_setfield(L, -2, "warning");

  /* ts.alert(...) */
  lua_pushcfunction(L, ts_lua_alert);
  lua_setfield(L, -2, "alert");

  /* ts.sleep(...) */
  lua_pushcfunction(L, ts_lua_sleep);
  lua_setfield(L, -2, "sleep");

  /* ts.schedule(...) */
  lua_pushcfunction(L, ts_lua_schedule);
  lua_setfield(L, -2, "schedule");

  /* ts.host_lookup(...) */
  lua_pushcfunction(L, ts_lua_host_lookup);
  lua_setfield(L, -2, "host_lookup");

  /* ts.get_install_dir(...) */
  lua_pushcfunction(L, ts_lua_get_install_dir);
  lua_setfield(L, -2, "get_install_dir");

  /* ts.get_config_dir(...) */
  lua_pushcfunction(L, ts_lua_get_config_dir);
  lua_setfield(L, -2, "get_config_dir");

  /* ts.get_runtime_dir(...) */
  lua_pushcfunction(L, ts_lua_get_runtime_dir);
  lua_setfield(L, -2, "get_runtime_dir");

  /* ts.get_plugin_dir(...) */
  lua_pushcfunction(L, ts_lua_get_plugin_dir);
  lua_setfield(L, -2, "get_plugin_dir");

  /* ts.get_traffic_server_version(...) */
  lua_pushcfunction(L, ts_lua_get_traffic_server_version);
  lua_setfield(L, -2, "get_traffic_server_version");

  ts_lua_inject_misc_variables(L);
}

static void
ts_lua_inject_misc_variables(lua_State *L)
{
  lua_pushinteger(L, TS_THREAD_POOL_NET);
  lua_setglobal(L, "TS_LUA_THREAD_POOL_NET");
  lua_pushinteger(L, TS_THREAD_POOL_TASK);
  lua_setglobal(L, "TS_LUA_THREAD_POOL_TASK");
}

static int
ts_lua_get_process_id(lua_State *L)
{
  const char *s;
  TSUuid process = TSProcessUuidGet();
  if (process) {
    s = TSUuidStringGet(process);
  } else {
    return luaL_error(L, "not able to get process uuid");
  }
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_now_time(lua_State *L)
{
  lua_Number now;

  // Return fractional seconds.
  now = ((lua_Number)TShrtime()) / 1000000000.0;
  lua_pushnumber(L, now);
  return 1;
}

static int
ts_lua_debug(lua_State *L)
{
  const char *msg;
  const char *flag;
  size_t msg_len = 0, flag_len = 0;

  if (lua_gettop(L) == 2) {
    msg  = luaL_checklstring(L, 2, &msg_len);
    flag = luaL_checklstring(L, 1, &flag_len);
    DbgCtl dc{flag};
    Dbg(dc, "%.*s", (int)msg_len, msg);
  } else {
    msg = luaL_checklstring(L, 1, &msg_len);
    Dbg(dbg_ctl, "%.*s", (int)msg_len, msg);
  }

  return 0;
}

static int
ts_lua_is_debug_tag_set(lua_State *L)
{
  const char *flag;
  size_t flag_len = 0;
  int stat        = 0;

  if (lua_gettop(L) == 1) {
    flag = luaL_checklstring(L, 1, &flag_len);
    stat = DbgCtl{flag}.on();
  } else {
    stat = dbg_ctl.on();
  }

  if (0 == stat) {
    lua_pushboolean(L, 0);
  } else {
    lua_pushboolean(L, 1);
  }

  return 1;
}

static int
ts_lua_error(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSError("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_emergency(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSEmergency("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_fatal(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSFatal("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_status(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSStatus("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_note(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSNote("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_warning(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSWarning("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_alert(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSAlert("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_schedule(lua_State *L)
{
  int sec;
  int type;
  int entry;

  ts_lua_http_ctx *actx;

  int n;

  TSCont contp;
  ts_lua_cont_info *ci;
  ts_lua_cont_info *nci;

  ci = ts_lua_get_cont_info(L);
  if (ci == nullptr) {
    return 0;
  }

  entry = lua_tointeger(L, 1);

  sec = luaL_checknumber(L, 2);
  if (sec < 1) {
    sec = 0;
  }

  type = lua_type(L, 3);
  if (type != LUA_TFUNCTION)
    return 0;

  n = lua_gettop(L);

  if (n < 3) {
    TSError("[ts_lua][%s] ts.schedule need at least three parameters", __FUNCTION__);
    return 0;
  }

  // TO-DO unset the original context in L
  actx = ts_lua_create_async_ctx(L, ci, n);

  contp = TSContCreate(ts_lua_schedule_handler, ci->mutex);
  TSContDataSet(contp, actx);

  nci        = &actx->cinfo;
  nci->contp = contp;
  nci->mutex = ci->mutex;

  TSContScheduleOnPool(contp, sec * 1000, TSThreadPool(entry));

  return 0;
}

static int
ts_lua_schedule_handler(TSCont contp, TSEvent ev, void *edata)
{
  lua_State *L;
  ts_lua_cont_info *ci;
  ts_lua_coroutine *crt;
  int event, n, ret;
  ts_lua_http_ctx *actx;
  ts_lua_main_ctx *main_ctx;

  event = (int)ev;
  Dbg(dbg_ctl, "getting actx and other info");
  actx = (ts_lua_http_ctx *)TSContDataGet(contp);

  Dbg(dbg_ctl, "getting http_Ctx");
  ci  = &actx->cinfo;
  crt = &ci->routine;

  main_ctx = crt->mctx;
  L        = crt->lua;

  TSMutexLock(main_ctx->mutexp);
  ts_lua_set_cont_info(L, ci);

  if (event == TS_LUA_EVENT_COROUTINE_CONT) {
    Dbg(dbg_ctl, "event is coroutine_cont");
    n   = (intptr_t)edata;
    ret = lua_resume(L, n);
  } else {
    Dbg(dbg_ctl, "event is not coroutine_cont");
    n   = lua_gettop(L);
    ret = lua_resume(L, n - 1);
  }

  if (ret == LUA_YIELD) {
    TSMutexUnlock(main_ctx->mutexp);
    goto done;
  }

  if (ret != 0) {
    TSError("[ts_lua][%s] lua_resume failed: %s", __FUNCTION__, lua_tostring(L, -1));
  }

  lua_pop(L, lua_gettop(L));
  TSMutexUnlock(main_ctx->mutexp);
  ts_lua_destroy_async_ctx(actx);

done:
  return 0;
}

static int
ts_lua_sleep(lua_State *L)
{
  int sec;
  TSAction action;
  TSCont contp;
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;

  ci = ts_lua_get_cont_info(L);
  if (ci == nullptr) {
    return 0;
  }

  sec = luaL_checknumber(L, 1);
  if (sec < 1) {
    sec = 1;
  }

  contp  = TSContCreate(ts_lua_sleep_handler, ci->mutex);
  action = TSContScheduleOnPool(contp, sec * 1000, TS_THREAD_POOL_NET);

  ai = ts_lua_async_create_item(contp, ts_lua_sleep_cleanup, (void *)action, ci);
  TSContDataSet(contp, ai);

  return lua_yield(L, 0);
}

static int
ts_lua_sleep_handler(TSCont contp, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;

  ai = static_cast<decltype(ai)>(TSContDataGet(contp));
  ci = ai->cinfo;

  ai->data = nullptr;
  ts_lua_sleep_cleanup(ai);

  TSContCall(ci->contp, TSEvent(TS_LUA_EVENT_COROUTINE_CONT), 0);

  return 0;
}

static int
ts_lua_sleep_cleanup(ts_lua_async_item *ai)
{
  if (ai->data) {
    TSActionCancel((TSAction)ai->data);
    ai->data = nullptr;
  }

  TSContDestroy(ai->contp);
  ai->deleted = 1;

  return 0;
}

static int
ts_lua_host_lookup(lua_State *L)
{
  const char *host;
  size_t host_len = 0;
  TSAction action;
  TSCont contp;
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;

  ci = ts_lua_get_cont_info(L);
  if (ci == nullptr) {
    return 0;
  }

  if (lua_gettop(L) != 1) {
    TSError("[ts_lua][%s] ts.host_lookup need at least one parameter", __FUNCTION__);
    return 0;
  }

  host = luaL_checklstring(L, 1, &host_len);

  contp = TSContCreate(ts_lua_host_lookup_handler, ci->mutex);
  ai    = ts_lua_async_create_item(contp, ts_lua_host_lookup_cleanup, nullptr, ci);

  TSContDataSet(contp, ai);
  action = TSHostLookup(contp, host, host_len);
  if (!TSActionDone(action)) {
    ai->data = (void *)action;
    return lua_yield(L, 0);
  }

  return 1;
}

static int
ts_lua_host_lookup_handler(TSCont contp, TSEvent event, void *edata)
{
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;
  char cip[128];
  lua_State *L;
  ts_lua_coroutine *crt;
  unsigned int resume;

  ai  = static_cast<decltype(ai)>(TSContDataGet(contp));
  ci  = ai->cinfo;
  crt = &ci->routine;
  L   = crt->lua;

  // find out if need to resume luaVM before async item cleanup
  if (ai->data != nullptr) {
    resume = 1;
  } else {
    resume = 0;
  }

  // async item cleanup
  ai->data = nullptr;
  ts_lua_host_lookup_cleanup(ai);

  if (event != TS_EVENT_HOST_LOOKUP) {
    TSError("[ts_lua][%s] ts.host_lookup receives unknown event", __FUNCTION__);
    lua_pushnil(L);
  } else if (!edata) {
    lua_pushnil(L);
  } else {
    TSHostLookupResult record   = (TSHostLookupResult)edata;
    struct sockaddr const *addr = TSHostLookupResultAddrGet(record);
    if (addr->sa_family == AF_INET) {
      inet_ntop(AF_INET, &((struct sockaddr_in const *)addr)->sin_addr, cip, sizeof(cip));
    } else if (addr->sa_family == AF_INET6) {
      inet_ntop(AF_INET6, &((struct sockaddr_in6 const *)addr)->sin6_addr, cip, sizeof(cip));
    } else {
      cip[0] = 0;
    }
    lua_pushstring(L, cip);
  }

  if (resume == 1) {
    TSContCall(ci->contp, TSEvent(TS_LUA_EVENT_COROUTINE_CONT), (void *)1);
  }

  return 0;
}

static int
ts_lua_host_lookup_cleanup(ts_lua_async_item *ai)
{
  if (ai->data) {
    TSActionCancel((TSAction)ai->data);
    ai->data = nullptr;
  }

  TSContDestroy(ai->contp);
  ai->deleted = 1;

  return 0;
}

static int
ts_lua_get_install_dir(lua_State *L)
{
  const char *s = TSInstallDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_config_dir(lua_State *L)
{
  const char *s = TSConfigDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_runtime_dir(lua_State *L)
{
  const char *s = TSRuntimeDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_plugin_dir(lua_State *L)
{
  const char *s = TSPluginDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_traffic_server_version(lua_State *L)
{
  const char *s = TSTrafficServerVersionGet();
  lua_pushstring(L, s);
  return 1;
}
