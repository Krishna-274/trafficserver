/*
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

#include <atomic>
#include <vector>
#include <cinttypes>

#include "swoc/swoc_file.h"

#include "ts/ts.h"
#include "swoc/TextView.h"
#include "regex.h"

using swoc::TextView;

namespace
{
constexpr char const PLUGIN_NAME[] = "TLS Bridge";
constexpr char const PLUGIN_TAG[]  = "tls_bridge";

// Base format string for making the internal CONNECT.
char const CONNECT_FORMAT[] = "CONNECT https://%.*s HTTP/1.1\r\n\r\n";

// TextView of the 'CONNECT' method string.
const TextView METHOD_CONNECT{TS_HTTP_METHOD_CONNECT, TS_HTTP_LEN_CONNECT};
constexpr TextView CONFIG_FILE_ARG{"--file"};
const std::string TS_CONFIG_DIR{TSConfigDirGet()};

DbgCtl dbg_ctl{PLUGIN_TAG};

}; // namespace

/* ------------------------------------------------------------------------------------ */
// Utility functions

/** Remove a field from a header.
    @a field is the name of the field, which is removed from the header specified by @a mbuf and @a hdr_loc.
*/
void
Hdr_Remove_Field(TSMBuffer mbuf, TSMLoc hdr_loc, TextView field)
{
  TSMLoc field_loc;
  if (TS_NULL_MLOC != (field_loc = TSMimeHdrFieldFind(mbuf, hdr_loc, field.data(), field.size()))) {
    TSMimeHdrFieldDestroy(mbuf, hdr_loc, field_loc);
    TSHandleMLocRelease(mbuf, hdr_loc, field_loc);
  }
}

/* ------------------------------------------------------------------------------------ */
/** Configuration data.
    This is a mapping of regular expressions to peer destinations. For an inbound CONNECT the destination
    is matched against the regular expressions. If matched the associated peer is used, otherwise the
    transaction is not intercepted.
 */
class BridgeConfig
{
  using self_type = BridgeConfig;

  /// Configuration item, regex -> destination.
  struct Item {
    using self_type = BridgeConfig;

    /// Construct an item.
    /// @internal Pass in the compiled regex because no instance of this is created if
    /// the regex doesn't compile successfully.
    Item(std::string_view pattern, Regex &&r, std::string_view service) : _pattern(pattern), _r(std::move(r)), _service(service) {}

    std::string _pattern; ///< Original configuration regular expression.
    Regex _r;             ///< Compiled regex.
    std::string _service; ///< Destination service if matched.
  };

public:
  /// Load the configuration from the command line args.
  void load_config(int argc, const char *argv[]);
  /// Get the number of configured matches.
  int count() const;
  /// Find a match for @a name.
  /// @return The destination or an empty view if no match.
  TextView match(TextView name);

private:
  /// Configuration item storage.
  std::vector<Item> _items;

  /** Load a configuration item pair.
   *
   * @param rxp The regular expression to match.
   * @param service The destination service.
   * @param ln Line number, or 0 if from plugin.config.
   */
  void load_pair(std::string_view rxp, std::string_view service, swoc::file::path const &src, int ln = 0);
};

inline int
BridgeConfig::count() const
{
  return _items.size();
}

void
BridgeConfig::load_pair(std::string_view rxp, std::string_view service, swoc::file::path const &src, int ln)
{
  Regex r;
  // Unfortunately PCRE can only compile null terminated strings...
  std::string pattern{rxp};
  if (r.compile(pattern.c_str(), Regex::ANCHORED)) {
    _items.emplace_back(rxp, std::move(r), service);
  } else {
    char buff[std::numeric_limits<int>::digits10 + 2] = "";
    if (ln) {
      snprintf(buff, sizeof(buff), " on line %d", ln);
    }
    TSError("[%s] Failed to compile regular expression '%.*s' in %s%s", PLUGIN_NAME, int(rxp.size()), rxp.data(), src.c_str(),
            buff);
  }
}

void
BridgeConfig::load_config(int argc, const char *argv[])
{
  static const swoc::file::path plugin_config_fp{"plugin.config"};

  for (int i = 0; i < argc; i += 2) {
    if (argv[i] == CONFIG_FILE_ARG) {
      if (i + 1 >= argc) {
        TSError("[%s] Invalid '%.*s' argument - no file name found.", PLUGIN_NAME, int(CONFIG_FILE_ARG.size()),
                CONFIG_FILE_ARG.data());
      } else {
        swoc::file::path fp(argv[i + 1]);
        std::error_code ec;
        if (!fp.is_absolute()) {
          fp = swoc::file::path{TS_CONFIG_DIR} / fp; // slap the config dir on it to make it absolute.
        }
        // bulk load the file.
        std::string content{swoc::file::load(fp, ec)};
        if (ec) {
          TSError("[%s] Invalid '%.*s' argument - unable to read file '%s' : %s.", PLUGIN_NAME, int(CONFIG_FILE_ARG.size()),
                  CONFIG_FILE_ARG.data(), fp.c_str(), ec.message().c_str());

        } else {
          // walk the lines.
          int line_no = 0;
          TextView src{content};
          while (!src.empty()) {
            TextView line{src.take_prefix_at('\n').trim_if(&isspace)};
            ++line_no;
            if (line.empty() || '#' == *line) {
              continue; // empty or comment, ignore.
            }

            // Pick apart the line into the regular expression and destination service.
            TextView service{line};
            TextView rxp{service.take_prefix_if(&isspace)};
            service.ltrim_if(&isspace); // dump extra separating space.
            // Only need to check service, as if the line isn't empty rxp will also be non-empty.
            if (service.empty()) {
              TSError("[%s] Invalid line %d in '%s' - no destination service found.", PLUGIN_NAME, line_no, fp.c_str());
            } else {
              this->load_pair(rxp, service, fp, line_no);
            }
          }
        }
      }
    } else if (argv[i][0] == '-') {
      TSError("[%s] Unrecognized option '%s'", PLUGIN_NAME, argv[i]);
      i -= 1; // Don't skip next arg.
    } else {
      if (i + 1 >= argc) {
        TSError("[%s] Regular expression '%s' without destination service", PLUGIN_NAME, argv[i]);
      } else {
        this->load_pair(argv[i], argv[i + 1], plugin_config_fp);
      }
    }
  }
}

TextView
BridgeConfig::match(TextView name)
{
  for (auto &item : _items) {
    if (item._r.exec(name)) {
      return {item._service};
    }
  }
  return {};
}

/// Global instance of the configuration.
BridgeConfig Config;

/* ------------------------------------------------------------------------------------ */
/// Operational Context object.
/// This holds all the data and methods for driving a TLS bridge.
struct Bridge {
  /// An I/O operation wrapper.
  struct Op {
    TSVIO _vio               = nullptr; ///< VIO for operation.
    TSIOBuffer _buff         = nullptr; ///< Buffer for operation.
    TSIOBufferReader _reader = nullptr; ///< Reader for operation.

    /// Initialize - set up buffer and reader.
    void init();
    /// Clean up.
    void close();
  };

  /// Per VConn data.
  struct VCData {
    TSVConn _vc = nullptr; ///< The virtual connection.
    Op _write;             ///< Write operational data.
    Op _read;              ///< Read operational data.

    /// Initialize - assign the VC and set up the IOBuffers and readers.
    void init(TSVConn vc);
    /// Start a read operation of size @a n.
    void do_read(TSCont cont, int64_t n);
    /// Start a write operation of size @a n.
    void do_write(TSCont cont, int64_t n);

    /// Get a view of the available data in the first block.
    /// This does @b not consume the data, it is a peek.
    TextView first_block_data();
    /// Get amount of available data for the read operation, if any.
    int64_t available_size();
    /// Consume @a n bytes of data.
    void consume(int64_t n);
    /// Close out the connection.
    void do_close();
  };

  TSCont _self_cont; ///< The continuation that handles events for @a this.
  TSHttpTxn _ua_txn; ///< User Agent transaction.
  TextView _peer;    ///< ATS peer for upstream connection.
  VCData _ua;        ///< User agent connection.
  VCData _out;       ///< Outbound connection.

  sockaddr const *_ua_addr; ///< User Agent address, needed for outbound connect.

  /// Parsing state for the response of the internal connect.
  enum OutboundState {
    PRE,    ///< Not ready to try it yet.
    OPEN,   ///< Initial internal CONNECT sent.
    OK,     ///< Received '200' local response.
    READY,  ///< Received local response terminal.
    STREAM, ///< In byte streaming mode.
    EOS,    ///< Streaming is done.
    ERROR,  ///< Upstream connection failure.
  } _out_resp_state = PRE;
  /// Track depth into outbound response terminal.
  int _out_terminal_pos = 0;
  /// Response code from upstream CONNECT
  TSHttpStatus _out_response_code = TS_HTTP_STATUS_NONE;
  /// Response reason, if not TS_HTTP_STATUS_OK
  std::string _out_response_reason;

  /// Bridge requires a continuation for scheduling and the transaction.
  Bridge(TSCont cont, TSHttpTxn txn, TextView peer);

  /// Called when the intercept (user agent) connection is set up.
  void net_accept(TSVConn ua_vc);
  /// Called when data is ready.
  void read_ready(TSVIO vio);
  /// Outbound reader, waiting for response code.
  /// @return @c true if response code found and moved to next state.
  bool check_outbound_OK();
  /// Outbound reader, waiting for response termination.
  /// @return @c true if terminal found and moved to next state.
  bool check_outbound_terminal();
  /// Outbound bulk reader.
  void outbound_reader(TSVIO vio);
  /// Handle EOS.
  void eos(TSVIO vio);
  /// Interfere with sending response to the user agent.
  void send_response_cb();
  /// Adjust the UA response to correspond to the actual upstream result.
  void update_ua_response();

  /// Move data from the outbound READ to the UA WRITE.
  void flow_to_ua();
  /// Move data from the UA READ to the outbound WRITE.
  void flow_to_outbound();
};

/// Used to generate IDs for the plugin connections.
std::atomic<int64_t> ConnectionCounter{0};

Bridge::Bridge(TSCont cont, TSHttpTxn txn, TextView peer) : _self_cont(cont), _ua_txn(txn), _peer(peer)
{
  _ua_addr = TSHttpTxnClientAddrGet(_ua_txn);
}

void
Bridge::net_accept(TSVConn vc)
{
  char buff[1024];
  int64_t n = snprintf(buff, sizeof(buff), CONNECT_FORMAT, int(_peer.size()), _peer.data());

  Dbg(dbg_ctl, "Received UA VConn, connecting to peer %.*s", int(_peer.size()), _peer.data());
  // UA side intercepted.
  _ua.init(vc);
  _ua.do_read(_self_cont, INT64_MAX);
  _ua.do_write(_self_cont, INT64_MAX);
  // Start up the outbound connect.
  _out.init(TSHttpConnectWithPluginId(_ua_addr, PLUGIN_TAG, ConnectionCounter++));
  _out_resp_state = OPEN;
  TSIOBufferWrite(_out._write._buff, buff, n);
  _out.do_write(_self_cont, n);
  TSVIOReenable(_out._write._vio);

  // Need to verify and strip off the outbound TS response to the internal connect.
  _out.do_read(_self_cont, INT64_MAX);
}

void
Bridge::read_ready(TSVIO vio)
{
  using swoc::TextView;

  Dbg(dbg_ctl, "READ READY");
  if (vio == _out._read._vio) {
    switch (_out_resp_state) {
    case PRE:
      break; // this should never happen.
    case ERROR:
      break;
    case EOS:
      break;
    case OPEN:
      if (!this->check_outbound_OK() || _out_resp_state != OK) {
        break;
      }
    // FALL THROUGH
    case OK:
      if (!this->check_outbound_terminal() || _out_resp_state != READY) {
        break;
      }
    // FALL THROUGH
    case READY:
      // Do setup for flowing upstream data to user agent.
      _out.do_write(_self_cont, INT64_MAX);
      TSVIOReenable(_out._write._vio);
      _out_resp_state = STREAM;
    // FALL THROUGH
    case STREAM:
      this->flow_to_ua();
      break;
    }
  } else if (vio == _ua._read._vio) {
    this->flow_to_outbound();
  }
}

bool
Bridge::check_outbound_OK()
{
  bool zret = false;
  TextView raw{_out.first_block_data()};

  // Only need to check the first block - it's guaranteed to be big enough to hold the status line
  // and the status line is always the first part of the response.
  // Looking for 'HTTP/#.# ### Reason text ...' where '#' is a digit.
  if (raw.size() > (8 + 3 + 1 + 3)) { // if not at least this much data, no chance of success.
    TextView block{raw};
    // Sigh, just unroll the check.
    if (block[0] == 'H' && block[1] == 'T' && block[2] == 'T' && block[3] == 'P' && block[4] == '/') {
      block += 5;
      if (block[1] == '.' && ((block[0] == '1' && (block[2] == '0' || block[2] == '1')) || (block[0] == '0' && block[2] == '9'))) {
        block += 3;
        block.ltrim_if(&isspace);
        TextView code  = block.take_prefix_if(&isspace);
        TSHttpStatus c = static_cast<TSHttpStatus>(swoc::svtoi(code));
        if (TS_HTTP_STATUS_OK == c) {
          _out_resp_state = OK;
        } else {
          // Save the reason provided from upstream.
          TextView reason = block.take_prefix_at('\r');
          _out_response_reason.assign(reason.data(), reason.size());
          _out_resp_state = ERROR;
        }
        // 519 is POOMA, useful for debugging, but may want to change this later.
        _out_response_code = c ? c : static_cast<TSHttpStatus>(519);
        _out.consume(block.data() - raw.data());
        zret = true;
        Dbg(dbg_ctl, "Outbound status %d", c);
      }
    }
  }
  return zret;
}

bool
Bridge::check_outbound_terminal()
{
  bool zret = false;
  TextView block;

  // Need to be more careful here than with the status check because the terminator can
  // be a large distance in to the response.
  while (READY != _out_resp_state && !(block = _out.first_block_data()).empty()) { // data is available
    // Loop through the bytes in the block data.
    int64_t n_bytes = 0;
    while (block) {
      char c = *block;
      if ('\r' == c) {
        if (_out_terminal_pos == 2) {
          _out_terminal_pos = 3;
        } else {
          _out_terminal_pos = 1;
        }
      } else if ('\n' == c) {
        if (_out_terminal_pos == 3) {
          _out_terminal_pos = 4;
          _out_resp_state   = READY;
          zret              = true;
          Dbg(dbg_ctl, "Outbound ready");
        } else if (_out_terminal_pos == 1) {
          _out_terminal_pos = 2;
        } else {
          _out_terminal_pos = 0;
        }
      } else {
        _out_terminal_pos = 0;
      }
      ++block;
      ++n_bytes;
    }
    _out.consume(n_bytes);
  }
  return zret;
}

void
Bridge::flow_to_ua()
{
  int64_t avail = _out.available_size();
  if (avail > 0) {
    int64_t n = TSIOBufferCopy(_ua._write._buff, _out._read._reader, avail, 0);
    // Assert for now, need to handle this more gracefully.
    TSAssert(n == avail);

    _out.consume(n);
    Dbg(dbg_ctl, "Wrote %" PRId64 " bytes to UA", n);
    TSVIOReenable(_ua._write._vio);
    TSVIOReenable(_out._read._vio);
  }
}

void
Bridge::flow_to_outbound()
{
  int64_t avail = _ua.available_size();
  if (avail > 0) {
    int64_t n = TSIOBufferCopy(_out._write._buff, _ua._read._reader, avail, 0);
    // Assert for now, need to handle this more gracefully.
    TSAssert(n == avail);

    _ua.consume(n);
    Dbg(dbg_ctl, "Wrote %" PRId64 " bytes to upstream", n);
    TSVIOReenable(_out._write._vio);
    TSVIOReenable(_ua._read._vio);
  }
}

void
Bridge::eos(TSVIO vio)
{
  if (nullptr == vio) {
    // Generic close for some non-EOS reason.
  } else if (vio == _out._write._vio || vio == _out._read._vio) {
    Dbg(dbg_ctl, "EOS upstream");
  } else if (vio == _ua._write._vio || vio == _ua._read._vio) {
    Dbg(dbg_ctl, "EOS user agent");
  } else {
    Dbg(dbg_ctl, "EOS from unknown VIO [%p]", vio);
  }
  _out.do_close();
  _ua.do_close();
  if (_out_resp_state != ERROR) {
    _out_resp_state = EOS;
  }
}

void
Bridge::send_response_cb()
{
  // This happens either after the upstream connection and the writing the response there,
  // or because the upstream connection was blocked. In either case the upstream work is
  // done and the original transaction can proceed.
  this->update_ua_response();
  TSHttpTxnReenable(_ua_txn, TS_EVENT_HTTP_CONTINUE);
}

void
Bridge::update_ua_response()
{
  TSMBuffer mbuf;
  TSMLoc hdr_loc;
  if (TS_SUCCESS == TSHttpTxnClientRespGet(_ua_txn, &mbuf, &hdr_loc)) {
    // If there is a non-200 upstream code then that's the most accurate because it was from
    // an actual upstream connection. Otherwise, let the original connection response code
    // ride.
    if (_out_response_code != TS_HTTP_STATUS_OK && _out_response_code != TS_HTTP_STATUS_NONE) {
      TSHttpHdrStatusSet(mbuf, hdr_loc, _out_response_code);
      if (!_out_response_reason.empty()) {
        TSHttpHdrReasonSet(mbuf, hdr_loc, _out_response_reason.data(), _out_response_reason.size());
      }
    }
    // TS insists on adding these fields, despite it being a CONNECT.
    Hdr_Remove_Field(mbuf, hdr_loc, {TS_MIME_FIELD_TRANSFER_ENCODING, TS_MIME_LEN_TRANSFER_ENCODING});
    Hdr_Remove_Field(mbuf, hdr_loc, {TS_MIME_FIELD_AGE, TS_MIME_LEN_AGE});
    Hdr_Remove_Field(mbuf, hdr_loc, {TS_MIME_FIELD_PROXY_CONNECTION, TS_MIME_LEN_PROXY_CONNECTION});
    TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdr_loc);
  } else {
    Dbg(dbg_ctl, "Failed to retrieve client response");
  }
}

void
Bridge::VCData::init(TSVConn vc)
{
  _vc = vc;
  _write.init();
  _read.init();
}

void
Bridge::VCData::do_read(TSCont cont, int64_t n)
{
  _read._vio = TSVConnRead(_vc, cont, _read._buff, n);
}

void
Bridge::VCData::do_write(TSCont cont, int64_t n)
{
  _write._vio = TSVConnWrite(_vc, cont, _write._reader, n);
}

void
Bridge::VCData::do_close()
{
  if (_vc) {
    TSVConnClose(_vc);
    _vc = nullptr;
  }
  _write.close();
  _read.close();
}

int64_t
Bridge::VCData::available_size()
{
  return TSIOBufferReaderAvail(_read._reader);
}

TextView
Bridge::VCData::first_block_data()
{
  TSIOBufferBlock b = TSIOBufferReaderStart(_read._reader);
  if (b) {
    int64_t k;
    const char *s = TSIOBufferBlockReadStart(b, _read._reader, &k);
    return {s, static_cast<size_t>(k)};
  }
  return {nullptr, 0};
}

void
Bridge::VCData::consume(int64_t n)
{
  TSIOBufferReaderConsume(_read._reader, n);
}

void
Bridge::Op::init()
{
  _buff   = TSIOBufferCreate();
  _reader = TSIOBufferReaderAlloc(_buff);
}

void
Bridge::Op::close()
{
  if (_reader) {
    TSIOBufferReaderFree(_reader);
    _reader = nullptr;
  }
  if (_buff) {
    TSIOBufferDestroy(_buff);
    _buff = nullptr;
  }
}

/* ------------------------------------------------------------------------------------ */
// Basically a dispatcher - look up the Bridge instance and call the appropriate method.
int
CB_Exec(TSCont contp, TSEvent ev_idx, void *data)
{
  auto ctx = static_cast<Bridge *>(TSContDataGet(contp));
  // No point in checking @a ctx for @c nullptr because if it's not there, neither is the
  // continuation so things would already be over the cliff.
  switch (ev_idx) {
  case TS_EVENT_NET_ACCEPT:
    ctx->net_accept(static_cast<TSVConn>(data));
    break;
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE:
    ctx->read_ready(static_cast<TSVIO>(data));
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    break;
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_VCONN_ACTIVE_TIMEOUT:
  case TS_EVENT_VCONN_EOS:
    ctx->eos(static_cast<TSVIO>(data));
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    Dbg(dbg_ctl, "SEND_RESPONSE_HDR");
    ctx->send_response_cb();
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    Dbg(dbg_ctl, "TXN_CLOSE: cleanup");
    ctx->eos(nullptr); // no specific VIO, close up everything.
    delete ctx;
    TSContDataSet(contp, nullptr); // Not sure if necessary, it's cheap, let's be safe.
    TSContDestroy(contp);
    break;
  default:
    Dbg(dbg_ctl, "Event %d", ev_idx);
    break;
  }
  return TS_EVENT_CONTINUE;
}

// Handle a new transaction - check if it should be intercepted and if so do the intercept.
int
CB_Read_Request_Hdr(TSCont contp, TSEvent ev_idx, void *data)
{
  auto txn = static_cast<TSHttpTxn>(data);
  TSMBuffer mbuf;
  TSMLoc hdr_loc;

  if (!TSHttpTxnIsInternal(txn)) {
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txn, &mbuf, &hdr_loc)) {
      int method_len;
      const char *method_data = TSHttpHdrMethodGet(mbuf, hdr_loc, &method_len);
      if (TextView{method_data, method_len} == METHOD_CONNECT) {
        int host_len          = 0;
        const char *host_name = TSHttpHdrHostGet(mbuf, hdr_loc, &host_len);
        TextView peer{Config.match({host_name, host_len})};
        if (peer) {
          // Everything checks, let's intercept.
          auto actor = TSContCreate(CB_Exec, TSContMutexGet(reinterpret_cast<TSCont>(txn)));
          auto ctx   = new Bridge(actor, txn, peer);

          Dbg(dbg_ctl, "Intercepting transaction %" PRIu64 " to '%.*s' via '%.*s'", TSHttpTxnIdGet(txn), host_len, host_name,
              static_cast<int>(peer.size()), peer.data());

          TSContDataSet(actor, ctx);
          // Need to play games with the response, delaying it until upstream connection is done.
          // Also may potentiall modify it to correspond to the upstream result.
          TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, actor);
          // Arrange for cleanup.
          TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, actor);
          // Skip remap and remap rule requirement - authorized by TLS bridge config.
          TSHttpTxnCntlSet(txn, TS_HTTP_CNTL_SKIP_REMAPPING, true);
          // Grab the transaction
          TSHttpTxnIntercept(actor, txn);
        }
      }
    }
  }
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_CONTINUE;
}

/* ------------------------------------------------------------------------------------ */

void
TSPluginInit(int argc, char const *argv[])
{
  TSPluginRegistrationInfo info{PLUGIN_NAME, "Oath:", "solidwallofcode@oath.com"};

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed.", PLUGIN_NAME);
  }

  Config.load_config(argc - 1, argv + 1);
  if (Config.count() <= 0) {
    TSError("[%s] No destinations defined, plugin disabled", PLUGIN_NAME);
  }

  TSCont contp = TSContCreate(CB_Read_Request_Hdr, TSMutexCreate());
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
