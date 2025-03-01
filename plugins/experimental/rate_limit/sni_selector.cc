/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "tscore/ink_config.h"

#include <cstring>

#include "sni_limiter.h"
#include "sni_selector.h"

///////////////////////////////////////////////////////////////////////////////
// This is the queue management continuation, which gets called periodically
//
static int
sni_queue_cont(TSCont cont, TSEvent event, void *edata)
{
  SniSelector *selector = static_cast<SniSelector *>(TSContDataGet(cont));

  for (const auto &[key, limiter] : selector->limiters()) {
    QueueTime now = std::chrono::system_clock::now(); // Only do this once per limiter

    // Try to enable some queued VCs (if any) if there are slots available
    while (limiter->size() > 0 && limiter->reserve()) {
      auto [vc, contp, start_time]    = limiter->pop();
      std::chrono::milliseconds delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

      (void)contp; // Ugly, but silences some compilers.
      Dbg(dbg_ctl, "SNI=%s: Enabling queued VC after %ldms", key.data(), static_cast<long>(delay.count()));
      TSVConnReenable(vc);
      limiter->incrementMetric(RATE_LIMITER_METRIC_RESUMED);
    }

    // Kill any queued VCs if they are too old
    if (limiter->size() > 0 && limiter->max_age > std::chrono::milliseconds::zero()) {
      now = std::chrono::system_clock::now(); // Update the "now", for some extra accuracy

      while (limiter->size() > 0 && limiter->hasOldEntity(now)) {
        // The oldest object on the queue is too old on the queue, so "kill" it.
        auto [vc, contp, start_time]  = limiter->pop();
        std::chrono::milliseconds age = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

        (void)contp;
        Dbg(dbg_ctl, "Queued VC is too old (%ldms), erroring out", static_cast<long>(age.count()));
        TSVConnReenableEx(vc, TS_EVENT_ERROR);
        limiter->incrementMetric(RATE_LIMITER_METRIC_EXPIRED);
      }
    }
  }

  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// This is the queue management continuation, which gets called periodically
//
bool
SniSelector::insert(std::string_view sni, SniRateLimiter *limiter)
{
  if (_limiters.find(sni) == _limiters.end()) {
    _limiters[sni] = limiter;
    Dbg(dbg_ctl, "Added global limiter for SNI=%s (limit=%u, queue=%u, max_age=%ldms)", sni.data(), limiter->limit,
        limiter->max_queue, static_cast<long>(limiter->max_age.count()));

    limiter->initializeMetrics(RATE_LIMITER_TYPE_SNI);

    return true;
  }

  return false;
}

SniRateLimiter *
SniSelector::find(std::string_view sni)
{
  if (sni.empty()) { // Likely shouldn't happen, but we can shortcircuit
    return nullptr;
  }

  auto limiter = _limiters.find(sni);

  if (limiter != _limiters.end()) {
    return limiter->second;
  }
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// This factory will create a number of SNI limiters based on the input string
// given. The list of SNI's is comma separated. ToDo: This should go away when
// we switch to a proper YAML parser, and we will only use the insert() above.
//
size_t
SniSelector::factory(const char *sni_list, int argc, const char *argv[])
{
  char *saveptr;
  char *sni   = strdup(sni_list); // We make a copy of the sni list, to not touch the original string
  char *token = strtok_r(sni, ",", &saveptr);

  // Todo: We are repeating initializing here with the same configurations, but once we move this to
  // YAML, and refactor this, it'll be better. And this is not particularly expensive.
  while (nullptr != token) {
    SniRateLimiter *limiter = new SniRateLimiter();
    TSReleaseAssert(limiter);

    limiter->initialize(argc, argv);
    limiter->description = token;

    _needs_queue_cont = (limiter->max_queue > 0);

    insert(std::string_view(limiter->description), limiter);
    token = strtok_r(nullptr, ",", &saveptr);
  }
  free(sni);

  return _limiters.size();
}

///////////////////////////////////////////////////////////////////////////////
// If needed, create the queue continuation that needs to run for this selector.
//
void
SniSelector::setupQueueCont()
{
  if (_needs_queue_cont && !_queue_cont) {
    _queue_cont = TSContCreate(sni_queue_cont, TSMutexCreate());
    TSReleaseAssert(_queue_cont);
    TSContDataSet(_queue_cont, this);
    _action = TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME.count(), TS_THREAD_POOL_TASK);
  }
}
