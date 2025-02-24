#!/usr/bin/env python3
#
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
#
import fileinput
# import pprint


def bases():
    pass


def indentp(str, indent):
    print("{}{}".format(" " * indent * 2, str))


def print_header():
    print("""
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
#pragma once

class IntConfig
{
public:
  IntConfig() = delete;
  explicit IntConfig(TSOverridableConfigKey key) : _key(key) {}

  // Implemented later in Configs.cc
  integer _get(Cript::Context *context) const;
  void _set(Cript::Context *context, integer value);

private:
  const TSOverridableConfigKey _key;
};

class FloatConfig
{
public:
  FloatConfig() = delete;
  explicit FloatConfig(TSOverridableConfigKey key) : _key(key) {}

  // Implemented later in Configs.cc
  float _get(Cript::Context *context) const;
  void _set(Cript::Context *context, float value);

private:
  const TSOverridableConfigKey _key;
};

""")


def print_class(tree, cur="", indent=0):
    firstInstance = True
    for k in tree.keys():
        if isinstance(tree[k], dict):
            if indent > 0:
                indentp("private:", indent - 1)
                if cur == "proxy":
                    indentp("friend class Cript::Context; // Needed to set the state", indent)
                    print()
            indentp("class {}".format(k.title()), indent)
            indentp("{", indent)
            print_class(tree[k], k, indent + 1)
        else:
            if firstInstance:
                indentp("public:", indent)
                firstInstance = False
            if tree[k][1] == "TS_RECORDDATATYPE_INT":
                indentp("IntConfig {}{{{}}};".format(k, tree[k][0]), indent + 1)
            elif tree[k][1] == "TS_RECORDDATATYPE_FLOAT":
                indentp("FloatConfig {}{{{}}};".format(k, tree[k][0]), indent + 1)
            elif tree[k][1] == "TS_RECORDDATATYPE_STRING":
                pass
                # ToDo: Support strings somehow??
                # indentp("StringConfig {}{{{}}};".format(k, tree[k][0]), indent + 1)
            else:
                print("The source file has a bad configuration data type: {}".format(tree[k][1]))
    if cur:
        indentp("}}; // End class {}".format(cur.title()), indent - 1)
        print()
        if cur != "proxy":
            indentp("public:", indent - 1)
            indentp("{} {};".format(cur.title(), cur), indent)
            print()


lines = list(fileinput.input())
i = 0
elements = []

while i < len(lines):
    line = lines[i].strip().rstrip()
    pos1 = line.find("proxy.config")
    if pos1 >= 0:
        pos2 = line.find("TS_CONFIG")
        if pos2 < 0:  # Split line
            line += lines[i + 1].strip().rstrip()
        (conf, enum, type) = line.split(",", 2)
        conf = conf.strip(" {\"").rstrip("\" ")
        enum = enum.strip(" {")
        type = type.strip(" ").rstrip(",}); ")
        # parts = conf.split(".")
        elements.append((conf, enum, type))
    i += 1


tree = {}
elements.sort()
for elem in elements:
    cur = tree
    parts = elem[0].split(".")
    size = len(parts) - 1
    for ix, part in enumerate(parts):
        sub = cur.get(part)
        if sub is None:
            if ix == size:
                cur[part] = (elem[1], elem[2])
            else:
                cur[part] = {}
            cur = cur[part]
        else:
            cur = sub

print_header()
print_class(tree)
# pp = pprint.PrettyPrinter(indent=4)
# pp.pprint(tree)
