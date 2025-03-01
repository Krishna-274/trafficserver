/** @file

  Event subsystem

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
#define _I_EventSystem_h

#include "tscore/ink_platform.h"
#include "ts/apidefs.h"

#include "I_IOBuffer.h"
#include "I_Action.h"
#include "I_Continuation.h"
#include "I_EThread.h"
#include "I_Event.h"
#include "I_EventProcessor.h"

#include "I_Lock.h"
#include "I_PriorityEventQueue.h"
#include "I_Processor.h"
#include "I_ProtectedQueue.h"
#include "I_Thread.h"
#include "I_VIO.h"
#include "I_VConnection.h"
#include "records/I_RecProcess.h"
#include "I_SocketManager.h"
#include "RecProcess.h"

static constexpr ts::ModuleVersion EVENT_SYSTEM_MODULE_PUBLIC_VERSION(1, 0, ts::ModuleVersion::PUBLIC);

void ink_event_system_init(ts::ModuleVersion version);
