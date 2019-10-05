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

#include "extensions/stackdriver/edges/edge_reporter.h"

#include "extensions/stackdriver/common/constants.h"
#include "extensions/stackdriver/edges/edges.pb.h"
#include "google/protobuf/util/time_util.h"

#ifndef NULL_PLUGIN
#include "api/wasm/cpp/proxy_wasm_intrinsics.h"
#else
#include "extensions/common/wasm/null/null_plugin.h"
#endif

namespace Extensions {
namespace Stackdriver {
namespace Edges {

using google::cloud::meshtelemetry::v1alpha1::ReportTrafficAssertionsRequest;
using google::cloud::meshtelemetry::v1alpha1::TrafficAssertion;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_GRPC;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_HTTP;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_HTTPS;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_TCP;
using google::cloud::meshtelemetry::v1alpha1::WorkloadInstance;
using google::protobuf::util::TimeUtil;

std::unique_ptr<WorkloadInstance> instanceFromMetadata(
    const ::wasm::common::NodeInfo& node_info) {
  auto instance = std::make_unique<WorkloadInstance>();
  // TODO(douglas-reid): support more than just kubernetes instances
  instance->set_uid("kubernetes://" + node_info.name() + "." +
                    node_info.namespace_());
  // TODO(douglas-reid): support more than just GCP ?
  instance->set_location(
      node_info.platform_metadata().at(Common::kGCPClusterLocationKey));
  instance->set_cluster_name(
      node_info.platform_metadata().at(Common::kGCPClusterNameKey));
  instance->set_owner_uid(node_info.owner());
  instance->set_workload_name(node_info.workload_name());
  instance->set_workload_namespace(node_info.namespace_());
  return instance;
};

EdgeReporter::EdgeReporter(
    const ::wasm::common::NodeInfo& local_node_info,
    std::unique_ptr<MeshEdgesServiceClient> edges_client) {
  current_request_ = std::make_unique<ReportTrafficAssertionsRequest>();

  const auto& project_id =
      local_node_info.platform_metadata().at(Common::kGCPProjectKey);
  current_request_->set_parent("projects/" + project_id);

  // TODO(dougreid): figure out how to get the real mesh uid here.
  // Using: //.../projects/<project_id>/<location>/meshes/<cluster> as a
  // placeholder.
  std::string location =
      local_node_info.platform_metadata().at(Common::kGCPClusterLocationKey);
  std::string cluster =
      local_node_info.platform_metadata().at(Common::kGCPClusterNameKey);
  std::string mesh_uid = "unknown";
  current_request_->set_mesh_uid(
      "//cloudresourcemanager.googleapis.com/projects/" + project_id + "/" +
      location + "/meshes/" + cluster);

  node_instance_ = instanceFromMetadata(local_node_info);

  edges_client_ = std::move(edges_client);
};

// ONLY inbound
void EdgeReporter::addEdge(const ::Wasm::Common::RequestInfo& request_info,
                           const ::wasm::common::NodeInfo& peer_node_info) {
  auto* traffic_assertions = current_request_->mutable_traffic_assertions();
  auto* edge = traffic_assertions->Add();

  edge->set_destination_service_name(request_info.destination_service_host);
  edge->set_destination_service_namespace(node_instance_->workload_namespace());
  edge->mutable_source()->CopyFrom(*instanceFromMetadata(peer_node_info).get());
  edge->mutable_destination()->CopyFrom(*node_instance_.get());

  auto protocol = request_info.request_protocol;
  if (protocol == "http" || protocol == "HTTP") {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_HTTP);
  } else if (protocol == "https" || protocol == "HTTPS") {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_HTTPS);
  } else if (protocol == "grpc" || protocol == "GRPC") {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_GRPC);
  } else {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_TCP);
  }

  if (current_request_->traffic_assertions_size() >
      max_assertions_per_request_) {
    flush();
  }
};

void EdgeReporter::reportEdges() {
  flush();
  for (auto& req : request_queue_) {
    edges_client_->reportTrafficAssertions(std::move(req));
  }
  request_queue_.clear();
}

void EdgeReporter::flush() {
  if (current_request_->traffic_assertions_size() == 0) {
    return;
  }

  std::unique_ptr<ReportTrafficAssertionsRequest> next =
      std::make_unique<ReportTrafficAssertionsRequest>();
  next->set_parent(current_request_->parent());
  next->set_mesh_uid(current_request_->mesh_uid());

  current_request_.swap(next);
  *next->mutable_timestamp() = TimeUtil::GetCurrentTime();
  request_queue_.emplace_back(std::move(next));
}

}  // namespace Edges
}  // namespace Stackdriver
}  // namespace Extensions
