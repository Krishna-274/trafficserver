/** @file
 *
 *  SETTINGS Frame Handler for Http3
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "Http3FrameHandler.h"
#include "Http3Session.h"

class Http3SettingsHandler : public Http3FrameHandler
{
public:
  Http3SettingsHandler(Http3Session *session) : _session(session){};

  // Http3FrameHandler
  std::vector<Http3FrameType> interests() override;
  Http3ErrorUPtr handle_frame(std::shared_ptr<const Http3Frame> frame, int32_t frame_seq = -1,
                              Http3StreamType s_type = Http3StreamType::UNKNOWN) override;

private:
  // TODO: clarify Http3Session I/F for Http3SettingsHandler and Http3App
  Http3Session *_session = nullptr;
};
