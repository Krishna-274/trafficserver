/** @file

   fuzzing iocore/net/ProxyProtocol

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
*/

#include "ProxyProtocol.h"
#include "tscore/Diags.h"

#define kMinInputLength 8
#define kMaxInputLength 1024

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 1;
  }

  swoc::TextView tv(reinterpret_cast<char *>((char *)input_data), size_data);

  DiagsPtr::set(new Diags("fuzzing", "", "", nullptr));

  ProxyProtocol pp_info;
  proxy_protocol_parse(&pp_info, tv);

  delete diags();

  return 0;
}
