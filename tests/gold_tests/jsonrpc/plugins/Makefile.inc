#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

noinst_LTLIBRARIES += gold_tests/jsonrpc/plugins/jsonrpc_plugin_handler_test.la
gold_tests_jsonrpc_plugins_jsonrpc_plugin_handler_test_la_SOURCES = gold_tests/jsonrpc/plugins/jsonrpc_plugin_handler_test.cc

AM_CPPFLAGS += \
    -I$(abs_top_srcdir)/mgmt \
    @YAMLCPP_INCLUDES@

gold_tests_jsonrpc_plugins_jsonrpc_plugin_handler_test_la_LDFLAGS = \
    $(AM_LDFLAGS) @SWOC_LDFLAGS@

gold_tests_jsonrpc_plugins_jsonrpc_plugin_handler_test_la_LIBADD = @SWOC_LIBS@

