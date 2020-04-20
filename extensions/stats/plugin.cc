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

#include "extensions/stats/plugin.h"

#include "absl/strings/ascii.h"
#include "extensions/common/util.h"
#include "google/protobuf/util/time_util.h"

using google::protobuf::util::TimeUtil;

// WASM_PROLOG
#ifndef NULL_PLUGIN
#include "extensions/common/proxy_expr.h"
#include "proxy_wasm_intrinsics.h"

#else  // NULL_PLUGIN

#include "extensions/common/wasm/null/null.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {
namespace Plugin {

#include "api/wasm/cpp/contrib/proxy_expr.h"

#endif  // NULL_PLUGIN

// END WASM_PROLOG

namespace Stats {

constexpr long long kDefaultTCPReportDurationMilliseconds = 15000;  // 15s

namespace {

// Efficient way to assign flatbuffer to a vector of strings.
// After C++17 could be simplified to a basic assignment.
#define FB_ASSIGN(name, v) \
  instance[name].assign((v) ? (v)->c_str() : nullptr, (v) ? (v)->size() : 0);
void map_node(IstioDimensions& instance, bool is_source,
              const ::Wasm::Common::FlatNode& node) {
  // Ensure all properties are set (and cleared when necessary).
  if (is_source) {
    FB_ASSIGN(source_workload, node.workload_name());
    FB_ASSIGN(source_workload_namespace, node.namespace_());

    auto source_labels = node.labels();
    if (source_labels) {
      auto app_iter = source_labels->LookupByKey("app");
      auto app = app_iter ? app_iter->value() : nullptr;
      FB_ASSIGN(source_app, app);

      auto version_iter = source_labels->LookupByKey("version");
      auto version = version_iter ? version_iter->value() : nullptr;
      FB_ASSIGN(source_version, version);

      auto canonical_name = source_labels->LookupByKey(
          ::Wasm::Common::kCanonicalServiceLabelName.data());
      auto name =
          canonical_name ? canonical_name->value() : node.workload_name();
      FB_ASSIGN(source_canonical_service, name);

      auto rev = source_labels->LookupByKey(
          ::Wasm::Common::kCanonicalServiceRevisionLabelName.data());
      if (rev) {
        FB_ASSIGN(source_canonical_revision, rev->value());
      } else {
        instance[source_canonical_revision] = ::Wasm::Common::kLatest.data();
      }
    } else {
      instance[source_app] = "";
      instance[source_version] = "";
      instance[source_canonical_service] = "";
      instance[source_canonical_revision] = ::Wasm::Common::kLatest.data();
    }
  } else {
    FB_ASSIGN(destination_workload, node.workload_name());
    FB_ASSIGN(destination_workload_namespace, node.namespace_());

    auto destination_labels = node.labels();
    if (destination_labels) {
      auto app_iter = destination_labels->LookupByKey("app");
      auto app = app_iter ? app_iter->value() : nullptr;
      FB_ASSIGN(destination_app, app);

      auto version_iter = destination_labels->LookupByKey("version");
      auto version = version_iter ? version_iter->value() : nullptr;
      FB_ASSIGN(destination_version, version);

      auto canonical_name = destination_labels->LookupByKey(
          ::Wasm::Common::kCanonicalServiceLabelName.data());
      auto name =
          canonical_name ? canonical_name->value() : node.workload_name();
      FB_ASSIGN(destination_canonical_service, name);

      auto rev = destination_labels->LookupByKey(
          ::Wasm::Common::kCanonicalServiceRevisionLabelName.data());
      if (rev) {
        FB_ASSIGN(destination_canonical_revision, rev->value());
      } else {
        instance[destination_canonical_revision] =
            ::Wasm::Common::kLatest.data();
      }
    } else {
      instance[destination_app] = "";
      instance[destination_version] = "";
      instance[destination_canonical_service] = "";
      instance[destination_canonical_revision] = ::Wasm::Common::kLatest.data();
    }

    FB_ASSIGN(destination_service_namespace, node.namespace_());
  }
}
#undef FB_ASSIGN

// Called during request processing.
void map_peer(IstioDimensions& instance, bool outbound,
              const ::Wasm::Common::FlatNode& peer_node) {
  map_node(instance, !outbound, peer_node);
}

void map_unknown_if_empty(IstioDimensions& instance) {
#define SET_IF_EMPTY(name)      \
  if (instance[name].empty()) { \
    instance[name] = unknown;   \
  }
  STD_ISTIO_DIMENSIONS(SET_IF_EMPTY)
#undef SET_IF_EMPTY
}

// maps from request context to dimensions.
// local node derived dimensions are already filled in.
void map_request(IstioDimensions& instance,
                 const ::Wasm::Common::RequestInfo& request) {
  instance[source_principal] = request.source_principal;
  instance[destination_principal] = request.destination_principal;
  instance[destination_service] = request.destination_service_host;
  instance[destination_service_name] = request.destination_service_name;
  instance[request_protocol] = request.request_protocol;
  std::string rclass;
  if (getValue({"filter_state", "istio.responseClass"}, &rclass)) {
    instance[response_code] = rclass;
  } else {
    instance[response_code] = std::to_string(request.response_code);
  }
  instance[response_flags] = request.response_flag;
  instance[connection_security_policy] = absl::AsciiStrToLower(std::string(
      ::Wasm::Common::AuthenticationPolicyString(request.service_auth_policy)));
}

// maps peer_node and request to dimensions.
void map(IstioDimensions& instance, bool outbound,
         const ::Wasm::Common::FlatNode& peer_node,
         const ::Wasm::Common::RequestInfo& request) {
  map_peer(instance, outbound, peer_node);
  map_request(instance, request);
  map_unknown_if_empty(instance);
  if (request.request_protocol == "grpc") {
    instance[grpc_response_status] = std::to_string(request.grpc_status);
  } else {
    instance[grpc_response_status] = "";
  }
  std::string operation;
  // istio.operationId can be populated by any plugin
  if (getValue({"filter_state", "istio.operationId"}, &operation)) {
    instance[request_operation] = operation;
  }
}

void clearTcpMetrics(::Wasm::Common::RequestInfo& request_info) {
  request_info.tcp_connections_opened = 0;
  request_info.tcp_sent_bytes = 0;
  request_info.tcp_received_bytes = 0;
}

}  // namespace

// Ordered dimension list is used by the metrics API.
const std::vector<MetricTag>& PluginRootContext::defaultTags() {
  static const std::vector<MetricTag> default_tags = {
#define DEFINE_METRIC_TAG(name) {#name, MetricTag::TagType::String},
      STD_ISTIO_DIMENSIONS(DEFINE_METRIC_TAG)
#undef DEFINE_METRIC_TAG
  };
  return default_tags;
}

const std::vector<MetricFactory>& PluginRootContext::defaultMetrics() {
  static const std::vector<MetricFactory> default_metrics = {
      // HTTP, HTTP/2, and GRPC metrics
      MetricFactory{
          "requests_total", MetricType::Counter,

          [](const ::Wasm::Common::RequestInfo&) -> uint64_t { return 1; },
          false},
      MetricFactory{
          "request_duration_milliseconds", MetricType::Histogram,
          [](const ::Wasm::Common::RequestInfo& request_info) -> uint64_t {
            return request_info.duration /* in nanoseconds */ / 1000000;
          },
          false},
      MetricFactory{"request_bytes", MetricType::Histogram,

                    [](const ::Wasm::Common::RequestInfo& request_info)
                        -> uint64_t { return request_info.request_size; },
                    false},
      MetricFactory{"response_bytes", MetricType::Histogram,

                    [](const ::Wasm::Common::RequestInfo& request_info)
                        -> uint64_t { return request_info.response_size; },
                    false},
      // TCP metrics.
      MetricFactory{"tcp_sent_bytes_total", MetricType::Counter,
                    [](const ::Wasm::Common::RequestInfo& request_info)
                        -> uint64_t { return request_info.tcp_sent_bytes; },
                    true},
      MetricFactory{"tcp_received_bytes_total", MetricType::Counter,
                    [](const ::Wasm::Common::RequestInfo& request_info)
                        -> uint64_t { return request_info.tcp_received_bytes; },
                    true},
      MetricFactory{
          "tcp_connections_opened_total", MetricType::Counter,
          [](const ::Wasm::Common::RequestInfo& request_info) -> uint64_t {
            return request_info.tcp_connections_opened;
          },
          true},
      MetricFactory{
          "tcp_connections_closed_total", MetricType::Counter,
          [](const ::Wasm::Common::RequestInfo& request_info) -> uint64_t {
            return request_info.tcp_connections_closed;
          },
          true},
  };
  return default_metrics;
}

void PluginRootContext::initializeDimensions() {
  // Clean-up existing expressions.
  cleanupExpressions();

  // Maps factory name to a factory instance
  Map<std::string, MetricFactory> factories;
  // Maps factory name to a list of tags.
  Map<std::string, std::vector<MetricTag>> metric_tags;
  // Maps factory name to a map from a tag name to an optional index.
  // Empty index means the tag needs to be removed.
  Map<std::string, Map<std::string, Optional<size_t>>> metric_indexes;

  // Seed the common metric tags with the default set.
  const std::vector<MetricTag>& default_tags = defaultTags();
  for (const auto& factory : defaultMetrics()) {
    factories[factory.name] = factory;
    metric_tags[factory.name] = default_tags;
    for (size_t i = 0; i < count_standard_labels; i++) {
      metric_indexes[factory.name][default_tags[i].name] = i;
    }
  }

  // Process the metric definitions (overriding existing).
  for (const auto& definition : config_.definitions()) {
    if (definition.name().empty() || definition.value().empty()) {
      continue;
    }
    auto token = addIntExpression(definition.value());
    auto& factory = factories[definition.name()];
    factory.name = definition.name();
    factory.extractor =
        [token](const ::Wasm::Common::RequestInfo&) -> uint64_t {
      int64_t result = 0;
      evaluateExpression(token.value(), &result);
      return result;
    };
    switch (definition.type()) {
      case stats::MetricType::COUNTER:
        factory.type = MetricType::Counter;
        break;
      case stats::MetricType::GAUGE:
        factory.type = MetricType::Gauge;
        break;
      case stats::MetricType::HISTOGRAM:
        factory.type = MetricType::Histogram;
        break;
      default:
        break;
    }
  }

  // Process the dimension overrides.
  for (const auto& metric : config_.metrics()) {
    // Sort tag override tags to keep the order of tags deterministic.
    std::vector<std::string> tags;
    const auto size = metric.dimensions().size();
    tags.reserve(size);
    for (const auto& dim : metric.dimensions()) {
      tags.push_back(dim.first);
    }
    std::sort(tags.begin(), tags.end());

    for (const auto& factory_it : factories) {
      if (!metric.name().empty() && metric.name() != factory_it.first) {
        continue;
      }
      auto& indexes = metric_indexes[factory_it.first];
      // Process tag deletions.
      for (const auto& tag : metric.tags_to_remove()) {
        auto it = indexes.find(tag);
        if (it != indexes.end()) {
          it->second = {};
        }
      }
      // Process tag overrides.
      for (const auto& tag : tags) {
        auto expr_index = addStringExpression(metric.dimensions().at(tag));
        Optional<size_t> value = {};
        if (expr_index.has_value()) {
          value = count_standard_labels + expr_index.value();
        }
        auto it = indexes.find(tag);
        if (it != indexes.end()) {
          it->second = value;
        } else {
          metric_tags[factory_it.first].push_back(
              {tag, MetricTag::TagType::String});
          indexes[tag] = value;
        }
      }
    }
  }

  // Local data does not change, so populate it on config load.
  istio_dimensions_.resize(count_standard_labels + expressions_.size());
  istio_dimensions_[reporter] = outbound_ ? source : destination;

  const auto& local_node =
      *flatbuffers::GetRoot<::Wasm::Common::FlatNode>(local_node_info_.data());
  map_node(istio_dimensions_, outbound_, local_node);

  // Instantiate stat factories using the new dimensions
  auto field_separator = CONFIG_DEFAULT(field_separator);
  auto value_separator = CONFIG_DEFAULT(value_separator);
  auto stat_prefix = CONFIG_DEFAULT(stat_prefix);

  // prepend "_" to opt out of automatic namespacing
  // If "_" is not prepended, envoy_ is automatically added by prometheus
  // scraper"
  stat_prefix = absl::StrCat("_", stat_prefix, "_");

  stats_ = std::vector<StatGen>();
  std::vector<MetricTag> tags;
  std::vector<size_t> indexes;
  for (const auto& factory_it : factories) {
    tags.clear();
    indexes.clear();
    size_t size = metric_tags[factory_it.first].size();
    tags.reserve(size);
    indexes.reserve(size);
    for (const auto& tag : metric_tags[factory_it.first]) {
      auto index = metric_indexes[factory_it.first][tag.name];
      if (index.has_value()) {
        tags.push_back(tag);
        indexes.push_back(index.value());
      }
    }
    stats_.emplace_back(stat_prefix, factory_it.second, tags, indexes,
                        field_separator, value_separator);
  }

  Metric build(MetricType::Gauge, absl::StrCat(stat_prefix, "build"),
               {MetricTag{"component", MetricTag::TagType::String},
                MetricTag{"tag", MetricTag::TagType::String}});
  build.record(
      1, "proxy",
      absl::StrCat(flatbuffers::GetString(local_node.istio_version()), ";"));
}

bool PluginRootContext::onConfigure(size_t) {
  std::unique_ptr<WasmData> configuration = getConfiguration();
  // Parse configuration JSON string.
  JsonParseOptions json_options;
  json_options.ignore_unknown_fields = true;
  Status status =
      JsonStringToMessage(configuration->toString(), &config_, json_options);
  if (status != Status::OK) {
    LOG_WARN(absl::StrCat("Cannot parse plugin configuration JSON string ",
                          configuration->toString()));
    return false;
  }

  if (!::Wasm::Common::extractLocalNodeFlatBuffer(&local_node_info_)) {
    LOG_WARN("cannot parse local node metadata ");
    return false;
  }
  outbound_ = ::Wasm::Common::TrafficDirection::Outbound ==
              ::Wasm::Common::getTrafficDirection();

  if (outbound_) {
    peer_metadata_id_key_ = ::Wasm::Common::kUpstreamMetadataIdKey;
    peer_metadata_key_ = ::Wasm::Common::kUpstreamMetadataKey;
  } else {
    peer_metadata_id_key_ = ::Wasm::Common::kDownstreamMetadataIdKey;
    peer_metadata_key_ = ::Wasm::Common::kDownstreamMetadataKey;
  }

  debug_ = config_.debug();
  use_host_header_fallback_ = !config_.disable_host_header_fallback();

  initializeDimensions();

  long long tcp_report_duration_milis = kDefaultTCPReportDurationMilliseconds;
  if (config_.has_tcp_reporting_duration()) {
    tcp_report_duration_milis =
        ::google::protobuf::util::TimeUtil::DurationToMilliseconds(
            config_.tcp_reporting_duration());
  }
  proxy_set_tick_period_milliseconds(tcp_report_duration_milis);

  return true;
}

void PluginRootContext::cleanupExpressions() {
  for (uint32_t token : expressions_) {
    exprDelete(token);
  }
  expressions_.clear();
  input_expressions_.clear();
  for (uint32_t token : int_expressions_) {
    exprDelete(token);
  }
  int_expressions_.clear();
}

Optional<size_t> PluginRootContext::addStringExpression(
    const std::string& input) {
  auto it = input_expressions_.find(input);
  if (it == input_expressions_.end()) {
    uint32_t token = 0;
    if (createExpression(input, &token) != WasmResult::Ok) {
      LOG_WARN(absl::StrCat("Cannot create an expression: " + input));
      return {};
    }
    size_t result = expressions_.size();
    input_expressions_[input] = result;
    expressions_.push_back(token);
    return result;
  }
  return it->second;
}

Optional<uint32_t> PluginRootContext::addIntExpression(
    const std::string& input) {
  uint32_t token = 0;
  if (createExpression(input, &token) != WasmResult::Ok) {
    LOG_WARN(absl::StrCat("Cannot create a value expression: " + input));
    return {};
  }
  int_expressions_.push_back(token);
  return token;
}

bool PluginRootContext::onDone() {
  cleanupExpressions();
  return true;
}

void PluginRootContext::onTick() {
  if (tcp_request_queue_.size() < 1) {
    return;
  }
  for (auto const& item : tcp_request_queue_) {
    // requestinfo is null, so continue.
    if (item.second == nullptr) {
      continue;
    }
    Context* context = getContext(item.first);
    if (context == nullptr) {
      continue;
    }
    context->setEffectiveContext();
    if (report(*item.second, true)) {
      // Clear existing data in TCP metrics, so that we don't double count the
      // metrics.
      clearTcpMetrics(*item.second);
    }
  }
}

bool PluginRootContext::report(::Wasm::Common::RequestInfo& request_info,
                               bool is_tcp) {
  std::string peer_id;
  getValue({peer_metadata_id_key_}, &peer_id);

  std::string peer;
  const ::Wasm::Common::FlatNode* peer_node =
      getValue({peer_metadata_key_}, &peer)
          ? flatbuffers::GetRoot<::Wasm::Common::FlatNode>(peer.data())
          : nullptr;

  // map and overwrite previous mapping.
  const ::Wasm::Common::FlatNode* destination_node_info =
      outbound_ ? peer_node
                : flatbuffers::GetRoot<::Wasm::Common::FlatNode>(
                      local_node_info_.data());
  std::string destination_namespace =
      destination_node_info && destination_node_info->namespace_()
          ? destination_node_info->namespace_()->str()
          : "";

  if (is_tcp) {
    // For TCP, if peer metadata is not available, peer id is set as not found.
    // Otherwise, we wait for metadata exchange to happen before we report  any
    // metric.
    // We keep waiting if response flags is zero, as that implies, there has
    // been no error in connection.
    uint64_t response_flags = 0;
    getValue({"response", "flags"}, &response_flags);
    if (peer_node == nullptr &&
        peer_id != ::Wasm::Common::kMetadataNotFoundValue &&
        response_flags == 0) {
      return false;
    }
    if (!request_info.is_populated) {
      ::Wasm::Common::populateTCPRequestInfo(outbound_, &request_info,
                                             destination_namespace);
    }
  } else {
    ::Wasm::Common::populateHTTPRequestInfo(outbound_, useHostHeaderFallback(),
                                            &request_info,
                                            destination_namespace);
  }

  map(istio_dimensions_, outbound_,
      peer_node ? *peer_node
                : *flatbuffers::GetRoot<::Wasm::Common::FlatNode>(
                      empty_node_info_.data()),
      request_info);
  for (size_t i = 0; i < expressions_.size(); i++) {
    if (!evaluateExpression(expressions_[i],
                            &istio_dimensions_.at(count_standard_labels + i))) {
      LOG_TRACE(absl::StrCat("Failed to evaluate expression at slot: " +
                             std::to_string(i)));
      istio_dimensions_[count_standard_labels + i] = "";
    }
  }

  auto stats_it = metrics_.find(istio_dimensions_);
  if (stats_it != metrics_.end()) {
    for (auto& stat : stats_it->second) {
      stat.record(request_info);
      LOG_DEBUG(
          absl::StrCat("metricKey cache hit ", ", stat=", stat.metric_id_));
    }
    cache_hits_accumulator_++;
    if (cache_hits_accumulator_ == 100) {
      incrementMetric(cache_hits_, cache_hits_accumulator_);
      cache_hits_accumulator_ = 0;
    }
    return true;
  }

  std::vector<SimpleStat> stats;
  for (auto& statgen : stats_) {
    if (statgen.is_tcp_metric() != is_tcp) {
      continue;
    }
    auto stat = statgen.resolve(istio_dimensions_);
    LOG_DEBUG(absl::StrCat("metricKey cache miss ", statgen.name(), " ",
                           ", stat=", stat.metric_id_));
    stat.record(request_info);
    stats.push_back(stat);
  }

  incrementMetric(cache_misses_, 1);
  // TODO: When we have c++17, convert to try_emplace.
  metrics_.emplace(istio_dimensions_, stats);
  return true;
}

void PluginRootContext::addToTCPRequestQueue(
    uint32_t id, std::shared_ptr<::Wasm::Common::RequestInfo> request_info) {
  tcp_request_queue_[id] = request_info;
}

void PluginRootContext::deleteFromTCPRequestQueue(uint32_t id) {
  tcp_request_queue_.erase(id);
}

#ifdef NULL_PLUGIN
NullPluginRegistry* context_registry_{};

class StatsFactory : public NullVmPluginFactory {
 public:
  std::string name() const override { return "envoy.wasm.stats"; }

  std::unique_ptr<NullVmPlugin> create() const override {
    return std::make_unique<NullPlugin>(context_registry_);
  }
};

static Registry::RegisterFactory<StatsFactory, NullVmPluginFactory> register_;
#endif

}  // namespace Stats

#ifdef NULL_PLUGIN
// WASM_EPILOG
}  // namespace Plugin
}  // namespace Null
}  // namespace Wasm
}  // namespace Common
}  // namespace Extensions
}  // namespace Envoy
#endif
