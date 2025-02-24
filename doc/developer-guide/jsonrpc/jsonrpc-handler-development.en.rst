.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. |RPC| replace:: JSONRPC 2.0

.. _JSONRPC: https://www.jsonrpc.org/specification
.. _JSON: https://www.json.org/json-en.html
.. _YAML: https://github.com/jbeder/yaml-cpp/wiki/Tutorial

.. _jsonrpc_development:

Handler implementation
**********************

Use this section as a guide for developing new rpc methods inside |TS| and how to expose them through the |RPC| endpoint.
Before we start, it is worth mentioning some of the architecture of the current implementation. The whole RPC mechanism is divided in
few components.

Json RPC manager
================

This class is the entrance point for both server calls and registered functions.

.. figure:: ../../uml/images/JsonRPCManager.svg

Dispatcher class
----------------

* Class that keeps track of all the registered methods and notifications that can be invoked by the RPC server. This class holds two
  hash tables containing methods and notifications which use the method name as a key.
* This class internally consumes ``RPCRequestInfo`` objects and performs the invocation of the respective calls.
* This class handlers the responses from the registered callbacks and it fills the appropriated ``RPCResponseInfo`` which then is passed
  back to the ``JsonRPCManager`` class.


JsonRPCManager class
--------------------

* Singleton class that handles the JSONRPC handler registration and JSONRPC handler invocation.
* This class is the main entrance point for the RPC server through the ``handle_call(std::string_view req)`` function.
* This class is the main entrance point for the handler to be able to register in the RPC logic. ``add_notification_handler`` and ``remove_notification_handler``.


Implementing new handlers
=========================

There a a few basic concepts that needs to be known before implementing a new handler, this is an easy process and the complexity depends on
the nature of the handler that you want to implement.
Dealing with incoming and outgoing parameters is up to the developer, we will touch on some ways to deal with this through this guide.

.. _jsonrpc_development-design:

Design
------

As requirement from the ``JsonRPCManager`` in order to be able to register inside the RPC management a function should implement the
following signature:

Methods:

.. code-block:: cpp

    ts::Rv<YAML::Node> your_rpc_handler_function_name(std::string_view const &id, YAML::Node const &params);

    // Plugins:
    void (*TSRPCMethodCb)(const char *id, TSYaml params);


Notifications:

.. code-block:: cpp

    void your_rpc_handler_function_name(YAML::Node const &params);

    // Plugins:
    void (*TSRPCNotificationCb)(TSYaml params);


* Incoming method request's id will be passed to the handler, this is read only value as the server is expected to respond with the same value.
* ``YAML::Node`` params is expected to be a ``Sequence`` or a ``Map``, as per protocol this cannot be a single value, so do not expect things like:
  ``param=123`` or ``param=SomeString``.
* The ``params`` can be empty and contains no data at all.

.. note::

    Plugins should cast ``TSYaml`` to a ``YAML::Node``. Regarding the `yamlcpp` library we will provide binary
    compatibility within the lifespan of a major release only.


It is important to know that ``method`` handlers are expected to respond to the requests, while ``notifications``` will not respond with
any data nor error. You can find more information in :ref:`jsonrpc-protocol` or directly in the protocol specs `JSONRPC`_.


.. note::

    If there is no explicit response from the method, the protocol implementation will respond with `success_response` unless an error
    was specified.


Registration and Handling
-------------------------

JSONRPC Manager API
~~~~~~~~~~~~~~~~~~~

Handler registration should be done by using the ``JsonRPCManager`` singleton object. Note that there are a set of convenient helper
functions that can be used to achieve registration through the singleton object.

.. code-block:: cpp

    namespace rpc {
        // this set of functions will call the singleton object and perform the same as by using the singleton directly.
        add_method_handler(...)
        add_notification_handler(...)
    }


.. code-block:: cpp

    // Handler implementation
    ts::Rv<YAML::Node>
    my_handler_impl(std::string_view const &id, YAML::Node const &params)
    {
        using namespace rpc::handlers::errors;
        return make_errata(Codes::SERVER, "Something happened in the server");
    }

The actual registration:

.. code-block:: cpp

    #include "rpc/jsonrpc/JsonRPC.h"
    ...
    rpc::add_method_handler("my_handler_impl", &my_handler_impl);


This API also accepts a RPCRegistryInfo pointer which will provide a context data for the particular handler, for instance it will
display the provider's name when the service descriptor gets called. There is a global object created for this purpose which can be used
As a generic registry context object,  ``core_ats_rpc_service_provider_handle`` is defined in the  ``JsonRPC.h`` header. Please check
:ref:`get_service_descriptor` for more information.


Notification example:

As mentioned before, notifications do not need to respond, as they are "fire and forget" calls, no id will be provided as part of the api.

.. code-block:: cpp

    void
    my_notification_handler(YAML::Node const &params)
    {
        // do something
        // all errors will be ignored by the server.
    }

Registration for notifications uses a different API:

.. code-block:: cpp

    #include "rpc/jsonrpc/JsonRPC.h"
    rpc::add_notification_handler("my_notification_handler", &my_notification_handler);



The registration API allows the client to pass a  ``RPCRegistryInfo`` which provides extra information for a particular handler. Non plugins handlers
should use the default provided Registry Info, located in the `JsonRPC.h` header file, ``core_ats_rpc_service_provider_handle``. Plugins should use the
one created by ``TSRPCRegister``

Plugin API
~~~~~~~~~~


Plugins have a different API to register and handle RPC. Unlike registering and handling rpc using the JSONRPC Manager directly, this
API provides a different approach, it's understood that plugins may not be able to respond a rpc with a valid response at the very
same moment that they are called, this could be because the rpc needs to perform an intensive operation or because the data that should
be returned by the handler is not yet ready, in any case plugin have the flexibility to say when they finished processing the request, either with
a successful response or with an error. **The JSONRPC manager will wait for the plugin to "mark" the current call as done**.

.. note:

    Check :c:func:`TSRPCRegister` for API details.


RPC method registration and implementation examples

#. No reschedule work on the rpc handler, we set the response in the same call. This runs on the RPC thread.

    .. code-block:: cpp

        #include <ts/ts.h>

        namespace {
            static const std::string MY_YAML_VERSION{"0.8.0"};
        }

        void
        my_join_string_handler(const char *id, TSYaml p)
        {
            YAML::Node params = *(YAML::Node *)p;
            // extract the strings from the params node
            std::vector<std::string> passedStrings;
            if (auto node = params["strings"]) {
                passedStrings = node.as<std::vector<std::string>>();
            } else {
                // We can't continue, let the JSONRPC Manager know that we have finished
                TSRPCHandlerError(NO_STRINGS_ERROR_CODE, " no strings field passed");
                return;
            }

            std::string join;
            std::for_each(std::begin(passedStrings), std::end(passedStrings), [&join](auto &&s) { join += s; });
            YAML::Node resp; // start building the response.
            resp["join"] = join; // add the join string into a "join" field.
            TSRPCHandlerDone(reinterpret_cast<TSYaml>(&resp)); // Let the JSONRPC Manager know that we have finished
        }

        void
        TSPluginInit(int argc, const char *argv[])
        {
            ...
            // Check-in to make sure we are compliant with the YAML version in TS.
            TSRPCProviderHandle rpcRegistrationInfo = TSRPCRegister("My plugin's info", "0.8.0");
            if (rpcRegistrationInfo == nullptr) {
                TSError("[%s] RPC handler registration failed, yaml version not supported.", PLUGIN_NAME);
            }

            if (TSRPCRegisterMethodHandler("join_strings", my_join_string_handler, rpcRegistrationInfo) == TS_ERROR) {
                Dbg(dbg_ctl, "%s failed to register", rpcCallName.c_str());
            } else {
                Dbg(dbg_ctl, "%s successfully registered", rpcCallName.c_str());
            }
        }

#. RPC handling rescheduled to run on a ET TASK thread.

    .. code-block:: cpp

        #include <ts/ts.h>

        namespace {
            static const std::string MY_YAML_VERSION{"0.8.0"};
        }

        int
        merge_yaml_file_on_et_task(TSCont contp, TSEvent event, void *data)
        {
            // read the incoming node and merge it with the copy on disk.
            YAML::Node params = *static_cast<YAML::Node *>(TSContDataGet(contp));

            // we only care for a map type {}
            if (params.Type() != YAML::NodeType::Map) {
                TSRPCHandlerError(INVALID_PARAM_TYPE_CODE, "Handler is expecting a map.");
                return TS_SUCCESS;
            }

            // do the actual work.
            merge_yaml(params);
            // ...
            YAML::Node resp;
            TSContDestroy(contp);
            TSRPCHandlerDone(reinterpret_cast<TSYaml>(&resp));
        }

        void
        merge_yaml_file(const char *id, TSYaml p)
        {
            TSCont c = TSContCreate(merge_yaml_file_on_et_task, TSMutexCreate());
            TSContDataSet(c, p);
            TSContScheduleOnPool(c, 3000 /* no particular reason */, TS_THREAD_POOL_TASK);
        }

        void
        TSPluginInit(int argc, const char *argv[])
        {
            // ...
            // Check-in to make sure we are compliant with the YAML version in TS.
            TSRPCProviderHandle rpcRegistrationInfo = TSRPCRegister("My plugin's info", "0.8.0");
            if (rpcRegistrationInfo == nullptr) {
                TSError("[%s] RPC handler registration failed, yaml version not supported.", PLUGIN_NAME);
            }

            if (TSRPCRegisterMethodHandler("merge_yaml_file", merge_yaml_file, rpcRegistrationInfo) == TS_ERROR) {
                Dbg(dbg_ctl, "%s failed to register", rpcCallName.c_str());
            } else {
                Dbg(dbg_ctl, "%s successfully registered", rpcCallName.c_str());
            }
        }


Error Handling
--------------


JSONRPC Manager API
~~~~~~~~~~~~~~~~~~~

There are several ways to deal with internal handler errors. Errors are expected to be sent back to the client if the API was expressed that way
and if the request was a ``method``.
We have defined some generic errors that can be used to respond depending on the nature of the registered handler,
please check :ref:`jsonrpc-handler-errors` for more info.

We recommend some ways to deal with this:

#. Using the ``Errata`` from ``ts::Rv<YAML::Node>``

This can be set in case you would like to let the server to respond with an |RPC| error, ``ExecutionError`` will be used to catch all the
errors that are fired from within the function call, either by setting the proper errata or by throwing an exception.
Please check the :ref:`jsonrpc-node-errors` and in particular ``ExecutionError``. Also check :ref:`jsonrpc-handler-errors`

.. important::

    Errors have preference over any other response, so if you set both, the errata and the ``YAML::Node`` response, then the former
    will be set in the response.

#. Defining a custom error object and make it part of the response object.

* This is up to the developer and the errors can be part of the response ``YAML::Node``.
* The JSONRPC Dispatcher will read that there is no error returned from the call and use the result to build the response. If this is
  what you are willing to respond to, then make sure that the error is not set in the ``ts::Rv``.


#. Exception.

    As long as the exception inherit from ``std::exception`` it will be handled by the jsonrpc manager, this error will be
    handled as like using the ``Errata`` object, this kind of errors will be part of the ``ExecutionError``.

    The following example will generate this JSON response:

    .. code-block:: cpp

        ts::Rv<YAML::Node>
        foo(std::string_view const &id, YAML::Node const &params)
        {
           some_unhandled_operation_that_throws();
        }


    .. code-block::json

        {
            "jsonrpc":"2.0",
            "error":{
                "code":9,
                "message":"Error during execution"
            },
            "id":"abcd-id"
        }


Examples:

* Respond with an error, no ``result`` field will be set in the response.

    .. code-block::

        ts::Rv<YAML::Node>
        respond_with_an_error(std::string_view const &id, YAML::Node const &params)
        {
            using namespace rpc::handlers::errors;
            return make_errata(Codes::SERVER, "Something happened in the server");
        }

    Server's response.

    .. code-block:: json

        {
            "jsonrpc":"2.0",
            "error":{
                "code":9,
                "message":"Error during execution",
                "data":[
                    {
                        "code":3000,
                        "message":"Something happened in the server"
                    }
                ]
            },
            "id":"abcd-id"
        }

    .. note::

        ``make_errata`` hides some internal details when creating an errata.

* Response with custom handler error. In this case, make sure that the API definition and documentation reflects this as so far we do not
  have json schemas to enforce any of this on the client side.


   .. code-block::

      ts::Rv<YAML::Node>
      respond_with_my_own_error(std::string_view const &id, YAML::Node const &params)
      {
          YAML::Node resp;
          resp["HandlerErrorDescription"] = "I can set up my own error in the result field.";
          return resp;
      }

    The "error" is part of the ``result``, in this case this could be used as any other field, the example would be the same.

   .. code-block:: json

      {
        "jsonrpc":"2.0",
        "result":{
            "HandlerErrorDescription":"I can set up my own error in the result field."
        },
        "id":"abcd-id"
      }


We have selected the ``ts::Rv<YAML::Node>`` as a message interface as this can hold the actual response/error.


Plugin API
~~~~~~~~~~

When implementing rpc handlers inside a plugin, errors should be handled differently:


.. code-block::

    #include <ts/ts.h>

    ...

    void
    foo_handler(const char *id, TSYaml p)
    {
        // read the incoming node and merge it with the copy on disk.
        YAML::Node params = *static_cast<YAML::Node *>(TSContDataGet(contp));

        if (check_if_error(params)) {
            TSRPCHandlerError(FOO_ERROR_CODE, "Some error descr.");
            return;
        }

        ...
    }


.. note::

    Errors can be a part of the request response(result field), this depends on the API design. The above example
    shows how to set an error that will be sent back as part of the Error field.


For more information check the  :c:func:`TSRPCRegister` for API details and :ref:`jsonrpc-error`.

.. _jsonrpc-handler-unit-test:

Unit test
=========

All new methods exposed through the RPC server can be tested using the jsonrpc autest extension.

jsonrpc_client.test.text
------------------------

This extension provides the ability to interact with the JSONRPC interface by using :program:`traffic_ctl` as a client. As a helper
for all new autest that needs to write and read jsonrpc message, there is also a new module `jsonrpc.py` which provides
a nice and easy interface to write methods and notifications.
This extension also provides the facility to write custom jsonrpc validations. Please check some of the following examples:


#. Write custom jsonrpc message

    .. code-block:: python

        '''
        The newly added jsonrpc method was named 'foo_bar' and is expected to accept a list of fqdn.
        '''
        tr = Test.AddTestRun("Test JSONRPC foo_bar()")
        '''
        The following call to the Request object will generate this:
        {
            "id": "850d32a8-d5a7-11eb-bebc-fa163e6d2ec5",
            "jsonrpc": "2.0",
            "method": "foo_bar",
            "params": {
                "fqdn": ["yahoo.com", "trafficserver.org"]
            }
        }
        '''
        req = Request.foo_bar(fqdn=["yahoo.com", "trafficserver.org"])
        tr.AddJsonRPCClientRequest(ts, req)


#. Custom response validation

    .. code-block:: python

        tr = Test.AddTestRun("Test update_host_status")

        Params = [
            {'name': 'yahoo', 'status': 'up'}
        ]

        tr.AddJsonRPCClientRequest(ts, Request.update_host_status(hosts=Params))

        def check_no_error_on_response(resp: Response):
            # we only check if it's an error.
            if resp.is_error():
                return (False, resp.error_as_str())
            return (True, "All good")

        tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(check_no_error_on_response)



CustomJSONRPCResponse
~~~~~~~~~~~~~~~~~~~~~~~

A tester class that will let you write your own response validation by dealing with the jsonrpc.Response object,
please check the ``CustomJSONRPCResponse`` tester for more information.


AddJsonRPCClientRequest
~~~~~~~~~~~~~~~~~~~~~~~

This function will generate a json response as an output, internally it uses :program:`traffic_ctl rpc file --format json` as client.
The output can be used and compared with a gold file. This also provides an optional schema validation for the entire JSONRPC protocol
as well as the ``param`` field against a specific schema file. You can specify ``schema_file_name`` with a valid json schema file to validate
the entire JSONRPC 2.0 request(except the content of the ``params`` field). You can also set ``params_field_schema_file_name`` with a
valid json schema file to validate only the ``params`` field.

Example:

    The following, beside sending the request, will perform a JSONRPC 2.0 schema validation as well as the ``param`` field.

    .. code-block:: python

        schema_file_name = 'schemas/jsonrpc20_request_schema.json'
        params_schema_file_name = 'schemas/join_strings_request_params_schema.json'
        tr.AddJsonRPCClientRequest(
            ts,
            file="join_strings_request.json",
            schema_file_name=schema_file_name,
            params_field_schema_file_name=params_schema_file_name)


JSONRPCResponseSchemaValidator
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A tester class to perform a schema validation of the entire JSONRPC 2.0 request as well as the ``result`` param.
You can specify ``schema_file_name`` with a valid json schema file to validate the entire JSONRPC 2.0 response(except the content of the ``result`` field).
Also you can set ``result_field_schema_file_name`` with a valid json schema file to validate only the ``result`` field.


Example:

    The following will add a Tester for a JSONRPC 2.0 schema validation as well as the ``result`` field.

    .. code-block:: python

        tr = Test.AddTestRun('test valid response schema')
        schema_file_name = 'schemas/jsonrpc20_response_schema.json'
        result_schema_file_name = 'schemas/join_strings_request_result_schema.json'

        # Add a tester.
        tr.Processes.Default.Streams.stdout = Testers.JSONRPCResponseSchemaValidator(
            schema_file_name=response_schema_file_name,
            result_field_schema_file_name=result_schema_file_name)

Important Notes
===============

* You can refer to `YAML`_ for more info in how code/decode yaml content.
* Remember to update :ref:`jsonrpc-api` if you are adding a new handler.
* If a new handler needs to be exposed through :program:`traffic_ctl` please refer to :ref:`traffic_ctl_jsonrpc` for a general idea
  and to :ref:`developer-guide-traffic_ctl-development` for how to implement a new command.
* To interact directly with the |RPC| node, please check :ref:`jsonrpc-node`


See also
========

:ref:`admin-jsonrpc-configuration`
:ref:`jsonrpc-protocol`
:ref:`developer-guide-traffic_ctl-development`


