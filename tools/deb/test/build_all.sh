#!/bin/bash
#
# Copyright 2017 Istio Authors. All Rights Reserved.
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

# Build debian and binaries for all components we'll test on the VM
# Will checkout or update from master, in the typical layout.
function build_all() {
  mkdir -p $GOPATH/src/istio.io

  for sub in pilot istio mixer auth proxy; do
    if [[ -d $GOPATH/src/istio.io/$sub ]]; then
      (cd $GOPATH/src/istio.io/$sub; git pull origin master)
    else
      (cd $GOPATH/src/istio.io; git clone https://github.com/istio/$sub)
    fi
  done

  # Note: components may still use old SHA - but the test will build the binaries from master
  # from each component, to make sure we don't test old code.
  pushd $GOPATH/src/istio.io/pilot
  bazel build ...
  ./bin/init.sh
  popd

  (cd $GOPATH/src/istio.io/mixer; bazel build ...)

  (cd $GOPATH/src/istio.io/proxy; bazel build tools/deb/...)

  (cd $GOPATH/src/istio.io/auth; bazel build ...)


}

build_all