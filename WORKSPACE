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

load(
    "//:repositories.bzl",
    "boringssl_repositories",
    "protobuf_repositories",
    "googletest_repositories",
)

boringssl_repositories()

protobuf_repositories()

googletest_repositories()

load(
    "//contrib/endpoints:repositories.bzl",
    "grpc_repositories",
    "mixer_client_repositories",
    "servicecontrol_client_repositories",
)

grpc_repositories()

mixer_client_repositories()

servicecontrol_client_repositories()

# Workaround for Bazel > 0.4.0 since it needs newer protobuf.bzl from:
# https://github.com/google/protobuf/pull/2246
# Do not use this git_repository for anything else than protobuf.bzl
new_git_repository(
    name = "protobuf_bzl",
    # Injecting an empty BUILD file to prevent using any build target
    build_file_content = "",
    commit = "05090726144b6e632c50f47720ff51049bfcbef6",
    remote = "https://github.com/google/protobuf.git",
)

load(
    "@mixerclient_git//:repositories.bzl",
    "mixerapi_repositories",
)

mixerapi_repositories(protobuf_repo="@protobuf_bzl//")

load(
    "//src/envoy:repositories.bzl",
    "envoy_repositories",
)

envoy_repositories()

load(
    "//test:repositories.bzl",
    "perl_repositories",
    "nginx_test_repositories",
)

perl_repositories()

nginx_test_repositories()
