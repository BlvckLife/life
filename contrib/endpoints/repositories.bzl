# Copyright 2016 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################
#

def servicecontrol_client_repositories(bind=True):
    native.git_repository(
        name = "servicecontrol_client_git",
        commit = "d739d755365c6a13d0b4164506fd593f53932f5d",
        remote = "https://github.com/cloudendpoints/service-control-client-cxx.git",
    )

    if bind:
        native.bind(
            name = "servicecontrol_client",
            actual = "@servicecontrol_client_git//:service_control_client_lib",
        )

def mixer_client_repositories(bind=True):
    native.git_repository(
        name = "mixerclient_git",
        commit = "1569430f1e27b31e23c029c6bec0d8d5062d9e55",
        remote = "https://github.com/istio/mixerclient.git",
    )

    if bind:
      native.bind(
          name = "mixer_client_lib",
          actual = "@mixerclient_git//:mixer_client_lib",
      )
