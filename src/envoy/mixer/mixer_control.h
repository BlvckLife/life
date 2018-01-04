/* Copyright 2017 Istio Authors. All Rights Reserved.
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

#include "control/include/http/controller.h"
#include "control/include/tcp/controller.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "src/envoy/mixer/config.h"
#include "src/envoy/mixer/stats.h"

namespace Envoy {
namespace Http {
namespace Mixer {

class MixerControlBase : public ThreadLocal::ThreadLocalObject {
 public:
  MixerControlBase(Event::Dispatcher& dispatcher,
                   const std::string& stats_prefix,
                   Stats::Scope& scope);

  void StatsUpdateCallback();
  void SetUpStatsTimer();

 protected:
  // These members are needed to update envoy stats periodically.
  MixerStatsObject stats_;

 private:
  Event::Dispatcher& dispatcher_;
  ::Envoy::Event::TimerPtr timer_;
};

class HttpMixerControl final : public MixerControlBase {
 public:
  // The constructor.
  HttpMixerControl(const HttpMixerConfig& mixer_config,
                   Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                   Runtime::RandomGenerator& random,
                   const std::string& stats_prefix,
                   Stats::Scope& scope);

  Upstream::ClusterManager& cm() { return cm_; }

  ::istio::mixer_control::http::Controller* controller() {
    return controller_.get();
  }

  bool has_v2_config() const { return has_v2_config_; }

 private:
  // Envoy cluster manager for making gRPC calls.
  Upstream::ClusterManager& cm_;
  // The mixer control
  std::unique_ptr<::istio::mixer_control::http::Controller> controller_;
  // has v2 config;
  bool has_v2_config_;
};

class TcpMixerControl final : public MixerControlBase {
 public:
  // The constructor.
  TcpMixerControl(const TcpMixerConfig& mixer_config,
                  Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                  Runtime::RandomGenerator& random,
                  const std::string& stats_prefix,
                  Stats::Scope& scope);

  ::istio::mixer_control::tcp::Controller* controller() {
    return controller_.get();
  }

 private:
  // The mixer control
  std::unique_ptr<::istio::mixer_control::tcp::Controller> controller_;
};

}  // namespace Mixer
}  // namespace Http
}  // namespace Envoy
