/** @file

  Some small general interest definitions

  @section license License

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

#pragma once

#include "tscore/ink_config.h"

#include <stddef.h> // NOLINT(modernize-deprecated-headers)

#ifdef HAVE_STDINT_H
#include <stdint.h> // NOLINT(modernize-deprecated-headers)
#endif

#ifdef HAVE_INTTYPES_H
#include <inttypes.h> // NOLINT(modernize-deprecated-headers)
#endif

#ifndef INT64_MIN
#define INT64_MAX (9223372036854775807LL)
#define INT64_MIN (-INT64_MAX - 1LL)
#define INT32_MAX (2147483647)
#define INT32_MIN (-2147483647 - 1)
#endif

#define POSIX_THREAD
#define POSIX_THREAD_10031c

#ifndef ETIME
#ifdef ETIMEDOUT
#define ETIME ETIMEDOUT
#endif
#endif

#ifndef ENOTSUP
#ifdef EOPNOTSUPP
#define ENOTSUP EOPNOTSUPP
#endif
#endif

#define NUL '\0'

// Determine the element count for an array.
#ifdef __cplusplus
template <typename T, unsigned N>
static inline unsigned
countof(const T (&)[N])
{
  return N;
}
#else
#define countof(x) ((unsigned)(sizeof(x) / sizeof((x)[0])))
#endif

#ifndef ABS
#define ABS(x) (((x) < 0) ? (-(x)) : (x))
#endif

#define ATS_UNUSED         __attribute__((unused))
#define ATS_WARN_IF_UNUSED __attribute__((warn_unused_result))
#define ATS_UNUSED_RETURN(x) \
  if (x) {                   \
  }

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define MAX_ALPN_STRING 30

/* Functions
 */
int ink_login_name_max();

#ifdef __cplusplus
// Round up a value to be a multiple of m if necessary.
//
template <typename ArithmeticV, typename ArithmeticM>
constexpr ArithmeticV
ROUNDUP(ArithmeticV value, ArithmeticM m)
{
  ArithmeticV remainder = value % m;
  if (remainder) {
    value += m - remainder;
  }
  return value;
}
#endif
