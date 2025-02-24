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

#include "ts_lua_util.h"
#include "ts_lua_remap.h"
#include "ts_lua_constant.h"
#include "ts_lua_client_request.h"
#include "ts_lua_server_request.h"
#include "ts_lua_server_response.h"
#include "ts_lua_client_response.h"
#include "ts_lua_cached_response.h"
#include "ts_lua_context.h"
#include "ts_lua_hook.h"
#include "ts_lua_http.h"
#include "ts_lua_misc.h"
#include "ts_lua_log.h"
#include "ts_lua_crypto.h"
#include "ts_lua_mgmt.h"
#include "ts_lua_package.h"
#include "ts_lua_stat.h"
#include "ts_lua_fetch.h"
#include "ts_lua_http_intercept.h"

static lua_State *ts_lua_new_state();
static void ts_lua_init_registry(lua_State *L);
static void ts_lua_init_globals(lua_State *L);
static void ts_lua_inject_ts_api(lua_State *L);
static ts_lua_ctx_stats *ts_lua_create_ctx_stats();
static void ts_lua_destroy_ctx_stats(ts_lua_ctx_stats *stats);
static void ts_lua_update_server_response_hdrp(ts_lua_http_ctx *http_ctx);

/**
   Update http_ctx->server_response_hdrp if there.

   This is required in the beginning of toughing response, because holding old pointer could be freed by core.
 */
void
ts_lua_update_server_response_hdrp(ts_lua_http_ctx *http_ctx)
{
  if (http_ctx->server_response_hdrp) {
    if (TSHttpTxnServerRespGet(http_ctx->txnp, &http_ctx->server_response_bufp, &http_ctx->server_response_hdrp) != TS_SUCCESS) {
      TSError("[ts_lua][%s] failed to get server response", __FUNCTION__);
      return;
    }
  }
}

void
ts_lua_clear_http_ctx(ts_lua_http_ctx *http_ctx)
{
  if (http_ctx->rri == nullptr) {
    if (http_ctx->client_request_url != nullptr) {
      TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, http_ctx->client_request_url);
      http_ctx->client_request_url = nullptr;
    }

    if (http_ctx->client_request_bufp != nullptr) {
      TSHandleMLocRelease(http_ctx->client_request_bufp, TS_NULL_MLOC, http_ctx->client_request_hdrp);
      http_ctx->client_request_bufp = nullptr;
      http_ctx->client_request_hdrp = nullptr;
    }
  }

  if (http_ctx->server_request_url != nullptr) {
    TSHandleMLocRelease(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, http_ctx->server_request_url);
    http_ctx->server_request_url = nullptr;
  }

  if (http_ctx->server_request_hdrp != nullptr) {
    TSHandleMLocRelease(http_ctx->server_request_bufp, TS_NULL_MLOC, http_ctx->server_request_hdrp);
    http_ctx->server_request_bufp = nullptr;
    http_ctx->server_request_hdrp = nullptr;
  }

  if (http_ctx->server_response_bufp != nullptr) {
    TSHandleMLocRelease(http_ctx->server_response_bufp, TS_NULL_MLOC, http_ctx->server_response_hdrp);
    http_ctx->server_response_bufp = nullptr;
    http_ctx->server_response_hdrp = nullptr;
  }

  if (http_ctx->client_response_hdrp != nullptr) {
    TSHandleMLocRelease(http_ctx->client_response_bufp, TS_NULL_MLOC, http_ctx->client_response_hdrp);
    http_ctx->client_response_bufp = nullptr;
    http_ctx->client_response_hdrp = nullptr;
  }

  if (http_ctx->cached_response_bufp != nullptr) {
    TSMimeHdrDestroy(http_ctx->cached_response_bufp, http_ctx->cached_response_hdrp);
    TSHandleMLocRelease(http_ctx->cached_response_bufp, TS_NULL_MLOC, http_ctx->cached_response_hdrp);
    TSMBufferDestroy(http_ctx->cached_response_bufp);
    http_ctx->cached_response_bufp = nullptr;
    http_ctx->cached_response_hdrp = nullptr;
  }
}

int
ts_lua_create_vm(ts_lua_main_ctx *arr, int n)
{
  int i;
  lua_State *L;

  for (i = 0; i < n; i++) {
    L = ts_lua_new_state();

    if (L == nullptr)
      return -1;

    lua_pushvalue(L, LUA_GLOBALSINDEX);

    arr[i].gref   = luaL_ref(L, LUA_REGISTRYINDEX); /* L[REG][gref] = L[GLOBAL] */
    arr[i].lua    = L;
    arr[i].mutexp = TSMutexCreate();
    arr[i].stats  = ts_lua_create_ctx_stats();
  }

  return 0;
}

void
ts_lua_destroy_vm(ts_lua_main_ctx *arr, int n)
{
  int i;
  lua_State *L;
  TSMutex mutexp;
  ts_lua_ctx_stats *stats;

  for (i = 0; i < n; i++) {
    L = arr[i].lua;
    if (L) {
      lua_close(L);
      arr[i].lua = nullptr;
    }
    mutexp = arr[i].mutexp;
    if (mutexp) {
      TSMutexDestroy(mutexp);
      arr[i].mutexp = nullptr;
    }

    stats = arr[i].stats;
    if (stats) {
      ts_lua_destroy_ctx_stats(stats);
      arr[i].stats = nullptr;
    }
  }

  return;
}

lua_State *
ts_lua_new_state()
{
  lua_State *L;

  L = luaL_newstate();

  if (L == nullptr) {
    return nullptr;
  }

  luaL_openlibs(L);

  ts_lua_init_registry(L);

  ts_lua_init_globals(L);

  return L;
}

ts_lua_ctx_stats *
ts_lua_create_ctx_stats()
{
  auto stats = tsapi::malloc<ts_lua_ctx_stats>();
  memset(stats, 0, sizeof(*stats));

  stats->mutexp = TSMutexCreate();

  return stats;
}

void
ts_lua_destroy_ctx_stats(ts_lua_ctx_stats *stats)
{
  if (stats) {
    TSMutexDestroy(stats->mutexp);
    stats->mutexp = nullptr;
    TSfree(stats);
  }
}

ts_lua_instance_conf *
ts_lua_script_registered(lua_State *L, char *script)
{
  TSMgmtInt curr_time;
  ts_lua_instance_conf *conf = nullptr;

  Dbg(dbg_ctl, "[%s] checking if script [%s] is registered", __FUNCTION__, script);

  // first check the reconfigure_time for the script. if it is not found, then it is new
  // if it matches the current reconfigure_time, then it is loaded already
  // And we return the conf pointer of it. Otherwise it can be loaded again.
  if (TS_SUCCESS == TSMgmtIntGet("proxy.process.proxy.reconfigure_time", &curr_time)) {
    lua_pushliteral(L, "__scriptTime");
    lua_pushstring(L, script);
    lua_concat(L, 2);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
      Dbg(dbg_ctl, "[%s] failed to get script time for [%s]", __FUNCTION__, script);
      lua_pop(L, -1);
      return nullptr;
    } else {
      int time = lua_tonumber(L, -1);
      lua_pop(L, -1);

      if (time == curr_time) {
        lua_pushliteral(L, "__scriptPtr");
        lua_pushstring(L, script);
        lua_concat(L, 2);
        lua_rawget(L, LUA_REGISTRYINDEX);
        if (lua_isnil(L, -1)) {
          Dbg(dbg_ctl, "[%s] failed to get script ptr for [%s]", __FUNCTION__, script);
          lua_pop(L, -1);
          return nullptr;
        } else {
          conf = static_cast<decltype(conf)>(lua_touserdata(L, -1));
          lua_pop(L, -1);
          return conf;
        }
      } else {
        Dbg(dbg_ctl, "[%s] script time not matching for [%s]", __FUNCTION__, script);
        return nullptr;
      }
    }

  } else {
    TSError("[ts_lua][%s] failed to get node's reconfigure time while checking script registration", __FUNCTION__);
    return nullptr;
  }
}

void
ts_lua_script_register(lua_State *L, char *script, ts_lua_instance_conf *conf)
{
  TSMgmtInt time;

  Dbg(dbg_ctl, "[%s] registering script [%s]", __FUNCTION__, script);

  // we recorded the script reconfigure_time and its conf pointer in registry
  if (TS_SUCCESS == TSMgmtIntGet("proxy.process.proxy.reconfigure_time", &time)) {
    lua_pushliteral(L, "__scriptTime");
    lua_pushstring(L, script);
    lua_concat(L, 2);
    lua_pushnumber(L, time);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_pushliteral(L, "__scriptPtr");
    lua_pushstring(L, script);
    lua_concat(L, 2);
    lua_pushlightuserdata(L, conf);
    lua_rawset(L, LUA_REGISTRYINDEX);
  } else {
    TSError("[ts_lua][%s] failed to get node's reconfigure time while registering script", __FUNCTION__);
  }
}

int
ts_lua_add_module(ts_lua_instance_conf *conf, ts_lua_main_ctx *arr, int n, int argc, char *argv[], char *errbuf, int errbuf_size)
{
  int i, ret;
  int t;
  lua_State *L;

  for (i = 0; i < n; i++) {
    conf->_first = (i == 0) ? 1 : 0;
    conf->_last  = (i == n - 1) ? 1 : 0;

    TSMutexLock(arr[i].mutexp);

    L = arr[i].lua;

    lua_newtable(L);                                /* new TB1 */
    lua_pushvalue(L, -1);                           /* new TB2 */
    lua_setfield(L, -2, "_G");                      /* TB1[_G] = TB2 empty table, we can change _G to xx */
    lua_newtable(L);                                /* new TB3 */
    lua_rawgeti(L, LUA_REGISTRYINDEX, arr[i].gref); /* push L[GLOBAL] */
    lua_setfield(L, -2, "__index");                 /* TB3[__index] = L[GLOBAL] which has ts.xxx api */
    lua_setmetatable(L, -2);                        /* TB1[META]  = TB3 */
    lua_replace(L, LUA_GLOBALSINDEX);               /* L[GLOBAL] = TB1 */

    ts_lua_set_instance_conf(L, conf);

    if (conf->content) {
      if (luaL_loadstring(L, conf->content)) {
        snprintf(errbuf, errbuf_size, "[%s] luaL_loadstring failed: %s", __FUNCTION__, lua_tostring(L, -1));
        lua_pop(L, 1);
        TSMutexUnlock(arr[i].mutexp);
        return -1;
      }

    } else if (strlen(conf->script)) {
      if (luaL_loadfile(L, conf->script)) {
        snprintf(errbuf, errbuf_size, "[%s] luaL_loadfile %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
        lua_pop(L, 1);
        TSMutexUnlock(arr[i].mutexp);
        return -1;
      }
    }

    if (lua_pcall(L, 0, 0, 0)) {
      snprintf(errbuf, errbuf_size, "[%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
      lua_pop(L, 1);
      TSMutexUnlock(arr[i].mutexp);
      return -1;
    }

    /* call "__init__", to parse parameters */
    lua_getglobal(L, "__init__");

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      // specifying that the file has an __init__ function
      conf->init_func = 1;

      lua_newtable(L);

      for (t = 0; t < argc; t++) {
        lua_pushnumber(L, t);
        lua_pushstring(L, argv[t]);
        lua_rawset(L, -3);
      }

      if (lua_pcall(L, 1, 1, 0)) {
        snprintf(errbuf, errbuf_size, "[%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
        lua_pop(L, 1);
        TSMutexUnlock(arr[i].mutexp);
        return -1;
      }

      ret = lua_tonumber(L, -1);
      lua_pop(L, 1);

      if (ret) {
        TSMutexUnlock(arr[i].mutexp);
        return -1; /* script parse error */
      }

    } else {
      lua_pop(L, 1); /* pop nil */
    }

    lua_pushlightuserdata(L, conf);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_rawset(L, LUA_REGISTRYINDEX); /* L[REG][conf] = L[GLOBAL] */

    lua_newtable(L);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = EMPTY */

    if (conf->ljgc > 0) {
      Dbg(dbg_ctl, "ljgc = %d, running LuaJIT Garbage Collector...", conf->ljgc);
      lua_gc(L, LUA_GCCOLLECT, 0);
    } else {
      Dbg(dbg_ctl, "ljgc = %d, NOT running LuaJIT Garbage Collector...", conf->ljgc);
    }

    TSMutexUnlock(arr[i].mutexp);
  }

  return 0;
}

int
ts_lua_del_module(ts_lua_instance_conf *conf, ts_lua_main_ctx *arr, int n)
{
  int i;
  lua_State *L;

  for (i = 0; i < n; i++) {
    TSMutexLock(arr[i].mutexp);

    L = arr[i].lua;

    /* call "__clean__", to clean resources */
    lua_pushlightuserdata(L, conf);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = L[REG][conf] */

    lua_getglobal(L, "__clean__"); /* get __clean__ function */

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      if (lua_pcall(L, 0, 0, 0)) {
        TSError("[ts_lua][%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
      }

    } else {
      lua_pop(L, 1); /* pop nil */
    }

    lua_pushlightuserdata(L, conf);

    if (conf->ref_count > 1) {
      Dbg(dbg_ctl, "Reference Count = %d , NOT clearing registry...", conf->ref_count);
      lua_pushvalue(L, LUA_GLOBALSINDEX);
      lua_rawset(L, LUA_REGISTRYINDEX); /* L[REG][conf] = L[GLOBAL] */
    } else {
      Dbg(dbg_ctl, "Reference Count = %d , clearing registry...", conf->ref_count);
      lua_pushnil(L);
      lua_rawset(L, LUA_REGISTRYINDEX); /* L[REG][conf] = nil */
    }

    lua_newtable(L);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = EMPTY  */

    if (conf->ljgc > 0) {
      Dbg(dbg_ctl, "ljgc = %d, running LuaJIT Garbage Collector...", conf->ljgc);
      lua_gc(L, LUA_GCCOLLECT, 0);
    } else {
      Dbg(dbg_ctl, "ljgc = %d, NOT running LuaJIT Garbage Collector...", conf->ljgc);
    }

    TSMutexUnlock(arr[i].mutexp);
  }

  return 0;
}

int
ts_lua_reload_module(ts_lua_instance_conf *conf, ts_lua_main_ctx *arr, int n)
{
  int i;
  lua_State *L;

  for (i = 0; i < n; i++) {
    TSMutexLock(arr[i].mutexp);

    L = arr[i].lua;

    /* call "__reload__", to clean resources if necessary */
    lua_pushlightuserdata(L, conf);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = L[REG][conf] */

    lua_getglobal(L, "__reload__"); /* get __clean__ function */

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      if (lua_pcall(L, 0, 0, 0)) {
        TSError("[ts_lua][%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
      }
    } else {
      lua_pop(L, 1); /* pop nil */
    }

    // new global context
    lua_newtable(L);                                /* new TB1 */
    lua_pushvalue(L, -1);                           /* new TB2 */
    lua_setfield(L, -2, "_G");                      /* TB1[_G] = TB2 empty table, we can change _G to xx */
    lua_newtable(L);                                /* new TB3 */
    lua_rawgeti(L, LUA_REGISTRYINDEX, arr[i].gref); /* push L[GLOBAL] */
    lua_setfield(L, -2, "__index");                 /* TB3[__index] = L[GLOBAL] which has ts.xxx api */
    lua_setmetatable(L, -2);                        /* TB1[META]  = TB3 */
    lua_replace(L, LUA_GLOBALSINDEX);               /* L[GLOBAL] = TB1 */

    ts_lua_set_instance_conf(L, conf);

    if (strlen(conf->script)) {
      if (luaL_loadfile(L, conf->script)) {
        TSError("[ts_lua][%s] luaL_loadfile %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
      } else {
        if (lua_pcall(L, 0, 0, 0)) {
          TSError("[ts_lua][%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
        }
      }
    }

    lua_pushlightuserdata(L, conf);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_rawset(L, LUA_REGISTRYINDEX); /* L[REG][conf] = L[GLOBAL] */

    lua_newtable(L);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = EMPTY */

    if (conf->ljgc > 0) {
      Dbg(dbg_ctl, "ljgc = %d, running LuaJIT Garbage Collector...", conf->ljgc);
      lua_gc(L, LUA_GCCOLLECT, 0);
    } else {
      Dbg(dbg_ctl, "ljgc = %d, NOT running LuaJIT Garbage Collector...", conf->ljgc);
    }

    TSMutexUnlock(arr[i].mutexp);
  }

  return 0;
}

int
ts_lua_init_instance(ts_lua_instance_conf *conf ATS_UNUSED)
{
  return 0;
}

int
ts_lua_del_instance(ts_lua_instance_conf *conf ATS_UNUSED)
{
  return 0;
}

static void
ts_lua_init_registry(lua_State *L ATS_UNUSED)
{
  return;
}

static void
ts_lua_init_globals(lua_State *L)
{
  ts_lua_inject_ts_api(L);
}

static void
ts_lua_inject_ts_api(lua_State *L)
{
  lua_newtable(L);

  ts_lua_inject_remap_api(L);
  ts_lua_inject_constant_api(L);

  ts_lua_inject_client_request_api(L);
  ts_lua_inject_server_request_api(L);
  ts_lua_inject_server_response_api(L);
  ts_lua_inject_client_response_api(L);
  ts_lua_inject_cached_response_api(L);
  ts_lua_inject_log_api(L);

  ts_lua_inject_context_api(L);
  ts_lua_inject_hook_api(L);

  ts_lua_inject_http_api(L);
  ts_lua_inject_intercept_api(L);
  ts_lua_inject_misc_api(L);
  ts_lua_inject_crypto_api(L);
  ts_lua_inject_mgmt_api(L);
  ts_lua_inject_package_api(L);
  ts_lua_inject_stat_api(L);
  ts_lua_inject_fetch_api(L);

  lua_getglobal(L, "package");
  lua_getfield(L, -1, "loaded");
  lua_pushvalue(L, -3);
  lua_setfield(L, -2, "ts");
  lua_pop(L, 2);

  lua_setglobal(L, "ts");
}

void
ts_lua_set_instance_conf(lua_State *L, ts_lua_instance_conf *conf)
{
  lua_pushliteral(L, "__ts_instance_conf");
  lua_pushlightuserdata(L, conf);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_instance_conf *
ts_lua_get_instance_conf(lua_State *L)
{
  ts_lua_instance_conf *conf;

  lua_pushliteral(L, "__ts_instance_conf");
  lua_rawget(L, LUA_GLOBALSINDEX);
  conf = static_cast<decltype(conf)>(lua_touserdata(L, -1));

  lua_pop(L, 1); // pop the conf out

  return conf;
}

void
ts_lua_set_cont_info(lua_State *L, ts_lua_cont_info *ci)
{
  lua_pushliteral(L, "__ts_cont_info");
  lua_pushlightuserdata(L, ci);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_cont_info *
ts_lua_get_cont_info(lua_State *L)
{
  ts_lua_cont_info *ci;

  lua_pushliteral(L, "__ts_cont_info");
  lua_rawget(L, LUA_GLOBALSINDEX);
  ci = static_cast<decltype(ci)>(lua_touserdata(L, -1));

  lua_pop(L, 1); // pop the coroutine out

  return ci;
}

ts_lua_http_ctx *
ts_lua_create_async_ctx(lua_State *L, ts_lua_cont_info *hci, int n)
{
  int i;
  lua_State *l;
  ts_lua_coroutine *crt;
  ts_lua_http_ctx *actx;

  actx = tsapi::malloc<ts_lua_http_ctx>();
  memset(actx, 0, sizeof(*actx));

  // create lua_thread
  l = lua_newthread(L);

  // init the coroutine
  crt       = &actx->cinfo.routine;
  crt->mctx = hci->routine.mctx;
  crt->lua  = l;
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);

  // update thread stats
  ts_lua_main_ctx *const main_ctx = crt->mctx;
  ts_lua_ctx_stats *const stats   = main_ctx->stats;

  TSMutexLock(stats->mutexp);
  ++stats->threads;
  if (stats->threads_max < stats->threads) {
    stats->threads_max = stats->threads;
  }
  TSMutexUnlock(stats->mutexp);

  // replace the param; start with 2 because first two params are not needed
  for (i = 2; i < n; i++) {
    lua_pushvalue(L, i + 1);
  }

  lua_xmove(L, l, n - 2);

  return actx;
}

void
ts_lua_destroy_async_ctx(ts_lua_http_ctx *http_ctx)
{
  ts_lua_cont_info *ci;

  ci = &http_ctx->cinfo;

  // update thread stats
  ts_lua_main_ctx *const main_ctx = ci->routine.mctx;
  ts_lua_ctx_stats *const stats   = main_ctx->stats;

  TSMutexLock(stats->mutexp);
  --stats->threads;
  TSMutexUnlock(stats->mutexp);

  ts_lua_release_cont_info(ci);
  TSfree(http_ctx);
}

void
ts_lua_set_http_ctx(lua_State *L, ts_lua_http_ctx *ctx)
{
  lua_pushliteral(L, "__ts_http_ctx");
  lua_pushlightuserdata(L, ctx);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_ctx *
ts_lua_get_http_ctx(lua_State *L)
{
  ts_lua_http_ctx *ctx;

  lua_pushliteral(L, "__ts_http_ctx");
  lua_rawget(L, LUA_GLOBALSINDEX);
  ctx = static_cast<decltype(ctx)>(lua_touserdata(L, -1));

  lua_pop(L, 1); // pop the ctx out

  return ctx;
}

ts_lua_http_ctx *
ts_lua_create_http_ctx(ts_lua_main_ctx *main_ctx, ts_lua_instance_conf *conf)
{
  ts_lua_coroutine *crt;
  ts_lua_http_ctx *http_ctx;
  lua_State *L;
  lua_State *l;

  L = main_ctx->lua;

  http_ctx = tsapi::malloc<ts_lua_http_ctx>();
  memset(http_ctx, 0, sizeof(*http_ctx));

  // create coroutine for http_ctx
  crt = &http_ctx->cinfo.routine;
  l   = lua_newthread(L);

  lua_pushlightuserdata(L, conf);
  lua_rawget(L, LUA_REGISTRYINDEX);

  /* new globals table for coroutine */
  lua_newtable(l);
  lua_pushvalue(l, -1);
  lua_setfield(l, -2, "_G");
  lua_newtable(l);
  lua_xmove(L, l, 1);
  lua_setfield(l, -2, "__index");
  lua_setmetatable(l, -2);

  lua_replace(l, LUA_GLOBALSINDEX);

  // init coroutine
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);
  crt->lua  = l;
  crt->mctx = main_ctx;

  // update thread stats
  ts_lua_ctx_stats *const stats = main_ctx->stats;

  TSMutexLock(stats->mutexp);
  ++stats->threads;
  if (stats->threads_max < stats->threads) {
    stats->threads_max = stats->threads;
  }
  TSMutexUnlock(stats->mutexp);

  http_ctx->instance_conf = conf;

  ts_lua_set_http_ctx(l, http_ctx);
  ts_lua_create_context_table(l);

  return http_ctx;
}

void
ts_lua_destroy_http_ctx(ts_lua_http_ctx *http_ctx)
{
  ts_lua_cont_info *ci;

  ci = &http_ctx->cinfo;

  if (http_ctx->rri == nullptr) {
    if (http_ctx->client_request_url) {
      TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, http_ctx->client_request_url);
    }

    if (http_ctx->client_request_bufp) {
      TSHandleMLocRelease(http_ctx->client_request_bufp, TS_NULL_MLOC, http_ctx->client_request_hdrp);
    }
  }

  if (http_ctx->server_request_url) {
    TSHandleMLocRelease(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, http_ctx->server_request_url);
  }

  if (http_ctx->server_request_bufp) {
    TSHandleMLocRelease(http_ctx->server_request_bufp, TS_NULL_MLOC, http_ctx->server_request_hdrp);
  }

  if (http_ctx->server_response_bufp) {
    TSHandleMLocRelease(http_ctx->server_response_bufp, TS_NULL_MLOC, http_ctx->server_response_hdrp);
  }

  if (http_ctx->client_response_bufp) {
    TSHandleMLocRelease(http_ctx->client_response_bufp, TS_NULL_MLOC, http_ctx->client_response_hdrp);
  }

  if (http_ctx->cached_response_bufp) {
    TSMimeHdrDestroy(http_ctx->cached_response_bufp, http_ctx->cached_response_hdrp);
    TSHandleMLocRelease(http_ctx->cached_response_bufp, TS_NULL_MLOC, http_ctx->cached_response_hdrp);
    TSMBufferDestroy(http_ctx->cached_response_bufp);
  }

  // update thread stats
  ts_lua_main_ctx *const main_ctx = ci->routine.mctx;
  ts_lua_ctx_stats *const stats   = main_ctx->stats;

  TSMutexLock(stats->mutexp);
  --stats->threads;
  TSMutexUnlock(stats->mutexp);

  ts_lua_release_cont_info(ci);
  TSfree(http_ctx);
}

void
ts_lua_set_http_intercept_ctx(lua_State *L, ts_lua_http_intercept_ctx *ictx)
{
  lua_pushliteral(L, "__ts_http_intercept_ctx");
  lua_pushlightuserdata(L, ictx);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_intercept_ctx *
ts_lua_get_http_intercept_ctx(lua_State *L)
{
  ts_lua_http_intercept_ctx *ictx;

  lua_pushliteral(L, "__ts_http_intercept_ctx");
  lua_rawget(L, LUA_GLOBALSINDEX);
  ictx = static_cast<decltype(ictx)>(lua_touserdata(L, -1));

  lua_pop(L, 1); // pop the ictx out

  return ictx;
}

ts_lua_http_intercept_ctx *
ts_lua_create_http_intercept_ctx(lua_State *L, ts_lua_http_ctx *http_ctx, int n)
{
  int i;
  lua_State *l;
  ts_lua_cont_info *hci;
  ts_lua_coroutine *crt;
  ts_lua_http_intercept_ctx *ictx;

  hci = &http_ctx->cinfo;

  ictx = tsapi::malloc<ts_lua_http_intercept_ctx>();
  memset(ictx, 0, sizeof(*ictx));

  ictx->hctx = http_ctx;

  // create lua_thread
  l = lua_newthread(L);

  // init the coroutine
  crt       = &ictx->cinfo.routine;
  crt->mctx = hci->routine.mctx;
  crt->lua  = l;
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);

  // Todo: replace the global, context table for crt->lua

  // replicate the param
  for (i = 0; i < n; i++) {
    lua_pushvalue(L, i + 1);
  }

  lua_xmove(L, l, n); // move the intercept function and params to the new lua_thread

  ts_lua_set_http_intercept_ctx(l, ictx);

  return ictx;
}

void
ts_lua_destroy_http_intercept_ctx(ts_lua_http_intercept_ctx *ictx)
{
  ts_lua_cont_info *ci;

  ci = &ictx->cinfo;

  if (ictx->net_vc) {
    TSVConnClose(ictx->net_vc);
  }

  TS_LUA_RELEASE_IO_HANDLE((&ictx->input));
  TS_LUA_RELEASE_IO_HANDLE((&ictx->output));

  ts_lua_release_cont_info(ci);
  TSfree(ictx);
}

void
ts_lua_set_http_transform_ctx(lua_State *L, ts_lua_http_transform_ctx *tctx)
{
  lua_pushliteral(L, "__ts_http_transform_ctx");
  lua_pushlightuserdata(L, tctx);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_transform_ctx *
ts_lua_get_http_transform_ctx(lua_State *L)
{
  ts_lua_http_transform_ctx *tctx;

  lua_pushliteral(L, "__ts_http_transform_ctx");
  lua_rawget(L, LUA_GLOBALSINDEX);
  tctx = static_cast<decltype(tctx)>(lua_touserdata(L, -1));

  lua_pop(L, 1); // pop the ictx out

  return tctx;
}

ts_lua_http_transform_ctx *
ts_lua_create_http_transform_ctx(ts_lua_http_ctx *http_ctx, TSVConn connp)
{
  lua_State *L;
  ts_lua_cont_info *hci;
  ts_lua_cont_info *ci;
  ts_lua_coroutine *crt;
  ts_lua_http_transform_ctx *transform_ctx;

  hci = &http_ctx->cinfo;
  L   = hci->routine.lua;

  transform_ctx = tsapi::malloc<ts_lua_http_transform_ctx>();
  memset(transform_ctx, 0, sizeof(*transform_ctx));

  transform_ctx->hctx = http_ctx;
  TSContDataSet(connp, transform_ctx);

  ci        = &transform_ctx->cinfo;
  ci->contp = connp;
  ci->mutex = TSContMutexGet((TSCont)http_ctx->txnp);

  crt       = &ci->routine;
  crt->mctx = hci->routine.mctx;
  crt->lua  = lua_newthread(L);
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);
  ts_lua_set_http_transform_ctx(crt->lua, transform_ctx);

  lua_pushlightuserdata(L, transform_ctx);
  lua_pushvalue(L, 2);
  lua_rawset(L, LUA_GLOBALSINDEX); // L[GLOBAL][transform_ctx] = transform handler

  return transform_ctx;
}

void
ts_lua_destroy_http_transform_ctx(ts_lua_http_transform_ctx *transform_ctx)
{
  ts_lua_cont_info *ci;

  if (!transform_ctx) {
    return;
  }

  ci = &transform_ctx->cinfo;

  TS_LUA_RELEASE_IO_HANDLE((&transform_ctx->output));
  TS_LUA_RELEASE_IO_HANDLE((&transform_ctx->reserved));

  ts_lua_release_cont_info(ci);

  TSfree(transform_ctx);
}

int
ts_lua_http_cont_handler(TSCont contp, TSEvent ev, void *edata)
{
  TSHttpTxn txnp;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  int event, ret, rc, n, t;
  lua_State *L;
  ts_lua_http_ctx *http_ctx;
  ts_lua_main_ctx *main_ctx;
  ts_lua_cont_info *ci;
  ts_lua_coroutine *crt;

  event    = (int)ev;
  http_ctx = (ts_lua_http_ctx *)TSContDataGet(contp);
  ci       = &http_ctx->cinfo;
  crt      = &ci->routine;

  main_ctx = crt->mctx;
  L        = crt->lua;

  txnp = http_ctx->txnp;

  rc = ret = 0;

  TSMutexLock(main_ctx->mutexp);

  if (!http_ctx->client_request_bufp) {
    if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
      http_ctx->client_request_bufp = bufp;
      http_ctx->client_request_hdrp = hdr_loc;

      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        http_ctx->client_request_url = url_loc;
      }
    }
  }

  if (!http_ctx->client_request_hdrp) {
    TSMutexUnlock(main_ctx->mutexp);

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  ts_lua_set_cont_info(L, ci);

  switch (event) {
  case TS_EVENT_HTTP_POST_REMAP:

    lua_getglobal(L, TS_LUA_FUNCTION_POST_REMAP);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:

    lua_getglobal(L, TS_LUA_FUNCTION_CACHE_LOOKUP_COMPLETE);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_SEND_REQUEST);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    ts_lua_update_server_response_hdrp(http_ctx);

    lua_getglobal(L, TS_LUA_FUNCTION_READ_RESPONSE);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_SEND_RESPONSE);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_READ_REQUEST);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_TXN_START:

    lua_getglobal(L, TS_LUA_FUNCTION_TXN_START);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_PRE_REMAP:

    lua_getglobal(L, TS_LUA_FUNCTION_PRE_REMAP);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_OS_DNS:

    lua_getglobal(L, TS_LUA_FUNCTION_OS_DNS);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_READ_CACHE_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_READ_CACHE);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    ts_lua_clear_http_ctx(http_ctx);

    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    ts_lua_update_server_response_hdrp(http_ctx);

    lua_getglobal(L, TS_LUA_FUNCTION_TXN_CLOSE);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      if (lua_pcall(L, 0, 1, 0)) {
        TSError("[ts_lua][%s] lua_pcall failed: %s", __FUNCTION__, lua_tostring(L, -1));
      }
    }

    ts_lua_destroy_http_ctx(http_ctx);
    break;

  case TS_LUA_EVENT_COROUTINE_CONT:
    n   = (intptr_t)edata;
    ret = lua_resume(L, n);

  default:
    break;
  }

  switch (ret) {
  case 0: // coroutine succeed
    t = lua_gettop(L);
    if (t > 0) {
      rc = lua_tointeger(L, -1);
      lua_pop(L, 1);
    }
    break;

  case LUA_YIELD: // coroutine yield
    rc = 1;
    break;

  default: // coroutine failed
    TSError("[ts_lua][%s] lua_resume failed: %s", __FUNCTION__, lua_tostring(L, -1));
    rc = -1;
    lua_pop(L, 1);
    break;
  }

  // current memory in use by this state
  int const gc_kb = lua_getgccount(L);

  TSMutexUnlock(main_ctx->mutexp);

  // collect state memory stats
  ts_lua_ctx_stats *const stats = main_ctx->stats;

  TSMutexLock(stats->mutexp);
  if (gc_kb != stats->gc_kb) {
    stats->gc_kb = gc_kb;
    if (stats->gc_kb_max < stats->gc_kb) {
      stats->gc_kb_max = stats->gc_kb;
    }
  }
  TSMutexUnlock(stats->mutexp);

  if (rc == 0) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  } else if (rc < 0) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);

  } else {
    // wait for async
  }

  return 0;
}

namespace ts_lua_ns
{
DbgCtl dbg_ctl{TS_LUA_DEBUG_TAG};
}
