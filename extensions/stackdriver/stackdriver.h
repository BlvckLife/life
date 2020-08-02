/* Copyright 2019 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "extensions/common/context.h"
#include "extensions/stackdriver/common/constants.h"
#include "extensions/stackdriver/config/v1alpha1/stackdriver_plugin_config.pb.h"
#include "extensions/stackdriver/edges/edge_reporter.h"
#include "extensions/stackdriver/log/logger.h"
#include "extensions/stackdriver/metric/record.h"

// OpenCensus is full of unused parameters in metric_service.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "opencensus/exporters/stats/stackdriver/stackdriver_exporter.h"
#pragma GCC diagnostic pop

#ifndef NULL_PLUGIN
#include "api/wasm/cpp/proxy_wasm_intrinsics.h"
#else

#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
#endif

namespace Stackdriver {

constexpr long int kDefaultEdgeNewReportDurationNanoseconds =
    60000000000;  // 1m
constexpr long int kDefaultEdgeEpochReportDurationNanoseconds =
    600000000000;                                                        // 10m
constexpr long int kDefaultTcpLogEntryTimeoutNanoseconds = 60000000000;  // 1m

#ifdef NULL_PLUGIN
PROXY_WASM_NULL_PLUGIN_REGISTRY;
#endif

// StackdriverRootContext is the root context for all streams processed by the
// thread. It has the same lifetime as the worker thread and acts as target for
// interactions that outlives individual stream, e.g. timer, async calls.
class StackdriverRootContext : public RootContext {
 public:
  StackdriverRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {
    ::Wasm::Common::extractEmptyNodeFlatBuffer(&empty_node_info_);
  }
  ~StackdriverRootContext() = default;

  bool onConfigure(size_t) override;
  bool configure(size_t);
  bool onStart(size_t) override;
  void onTick() override;
  bool onDone() override;

  // Get direction of traffic relative to this proxy.
  bool isOutbound();

  bool useHostHeaderFallback() const { return use_host_header_fallback_; };

  // Records telemetry for the current active stream/connection. Returns true,
  // if request was recorded.
  bool recordTCP(uint32_t id);
  // Records telemetry for the current active stream.
  void record();
  // Functions for TCP connection's RequestInfo queue.
  void addToTCPRequestQueue(uint32_t id);
  void deleteFromTCPRequestQueue(uint32_t id);
  void incrementReceivedBytes(uint32_t id, size_t size);
  void incrementSentBytes(uint32_t id, size_t size);
  void incrementConnectionClosed(uint32_t id);
  void setConnectionState(uint32_t id,
                          ::Wasm::Common::TCPConnectionState state);

  bool getPeerId(std::string& peer_id) {
    bool found =
        getValue({isOutbound() ? ::Wasm::Common::kUpstreamMetadataIdKey
                               : ::Wasm::Common::kDownstreamMetadataIdKey},
                 &peer_id);
    return found;
  }
  const ::Wasm::Common::FlatNode& getLocalNode() {
    return *flatbuffers::GetRoot<::Wasm::Common::FlatNode>(
        local_node_info_.data());
  }

  bool initialized() const { return initialized_; };

 private:
  // Stores information about TCP request.
  struct TcpRecordInfo {
    std::unique_ptr<::Wasm::Common::RequestInfo> request_info;
    bool tcp_open_entry_logged;
  };

  // Indicates whether to export server access log or not.
  bool enableServerAccessLog();

  bool shouldLogThisRequest(::Wasm::Common::RequestInfo& request_info);

  // Indicates whether or not to report edges to Stackdriver.
  bool enableEdgeReporting();

  // Indicates whether or not to report TCP Logs.
  bool enableTCPServerAccessLog();

  // Config for Stackdriver plugin.
  stackdriver::config::v1alpha1::PluginConfig config_;

  // Local node info extracted from node metadata.
  std::string local_node_info_;
  std::string empty_node_info_;

  // Indicates the traffic direction relative to this proxy.
  ::Wasm::Common::TrafficDirection direction_{
      ::Wasm::Common::TrafficDirection::Unspecified};

  // Logger records and exports log entries to Stackdriver backend.
  std::unique_ptr<::Extensions::Stackdriver::Log::Logger> logger_;

  std::unique_ptr<::Extensions::Stackdriver::Edges::EdgeReporter>
      edge_reporter_;

  long int last_edge_epoch_report_call_nanos_ = 0;

  long int last_edge_new_report_call_nanos_ = 0;

  long int edge_new_report_duration_nanos_ =
      kDefaultEdgeNewReportDurationNanoseconds;

  long int edge_epoch_report_duration_nanos_ =
      kDefaultEdgeEpochReportDurationNanoseconds;

  long int tcp_log_entry_timeout_ = kDefaultTcpLogEntryTimeoutNanoseconds;

  bool use_host_header_fallback_;
  bool initialized_ = false;

  std::unordered_map<uint32_t,
                     std::unique_ptr<StackdriverRootContext::TcpRecordInfo>>
      tcp_request_queue_;
};

// StackdriverContext is per stream context. It has the same lifetime as
// the request stream itself.
class StackdriverContext : public Context {
 public:
  StackdriverContext(uint32_t id, RootContext* root)
      : Context(id, root),
        is_tcp_(false),
        context_id_(id),
        is_initialized_(getRootContext()->initialized()) {}
  void onLog() override;

  FilterStatus onNewConnection() override {
    if (!is_initialized_) {
      return FilterStatus::Continue;
    }

    is_tcp_ = true;
    getRootContext()->addToTCPRequestQueue(context_id_);
    getRootContext()->setConnectionState(
        context_id_, ::Wasm::Common::TCPConnectionState::Open);
    return FilterStatus::Continue;
  }

  // Called on onData call, so counting the data that is received.
  FilterStatus onDownstreamData(size_t size, bool) override {
    if (!is_initialized_) {
      return FilterStatus::Continue;
    }
    getRootContext()->incrementReceivedBytes(context_id_, size);
    getRootContext()->setConnectionState(
        context_id_, ::Wasm::Common::TCPConnectionState::Connected);
    return FilterStatus::Continue;
  }
  // Called on onWrite call, so counting the data that is sent.
  FilterStatus onUpstreamData(size_t size, bool) override {
    if (!is_initialized_) {
      return FilterStatus::Continue;
    }
    getRootContext()->incrementSentBytes(context_id_, size);
    getRootContext()->setConnectionState(
        context_id_, ::Wasm::Common::TCPConnectionState::Connected);
    return FilterStatus::Continue;
  }

 private:
  // Gets root Stackdriver context that this stream Stackdriver context
  // associated with.
  StackdriverRootContext* getRootContext();

  bool is_tcp_;
  uint32_t context_id_;
  const bool is_initialized_;
};

class StackdriverOutboundRootContext : public StackdriverRootContext {
 public:
  StackdriverOutboundRootContext(uint32_t id, std::string_view root_id)
      : StackdriverRootContext(id, root_id) {}
};

class StackdriverInboundRootContext : public StackdriverRootContext {
 public:
  StackdriverInboundRootContext(uint32_t id, std::string_view root_id)
      : StackdriverRootContext(id, root_id) {}
};

static RegisterContextFactory register_OutboundStackdriverContext(
    CONTEXT_FACTORY(StackdriverContext),
    ROOT_FACTORY(StackdriverOutboundRootContext),
    ::Extensions::Stackdriver::Common::kOutboundRootContextId);
static RegisterContextFactory register_InboundStackdriverContext(
    CONTEXT_FACTORY(StackdriverContext),
    ROOT_FACTORY(StackdriverInboundRootContext),
    ::Extensions::Stackdriver::Common::kInboundRootContextId);

}  // namespace Stackdriver

#ifdef NULL_PLUGIN
}  // namespace null_plugin
}  // namespace proxy_wasm
#endif
