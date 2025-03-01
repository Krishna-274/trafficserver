/** @file

  Http1ServerTransaction.cc - The Server Transaction class for Http1*

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

#include "Http1ServerTransaction.h"
#include "Http1ServerSession.h"

void
Http1ServerTransaction::release()
{
  _proxy_ssn->release(this);
}

void
Http1ServerTransaction::increment_transactions_stat()
{
  Metrics::increment(http_rsb.current_server_transactions);
}

void
Http1ServerTransaction::decrement_transactions_stat()
{
  Metrics::decrement(http_rsb.current_server_transactions);
}

void
Http1ServerTransaction::transaction_done()
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  super_type::transaction_done();
  if (_proxy_ssn) {
    static_cast<Http1ServerSession *>(_proxy_ssn)->release_transaction();
  }
}
