/* Copyright 2018 Istio Authors. All Rights Reserved.
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

#include "src/envoy/utils/authn.h"
#include "common/common/base64.h"
#include "include/istio/utils/attribute_names.h"
#include "src/envoy/utils/filter_names.h"
#include "src/istio/authn/context.pb.h"

using istio::authn::Result;

namespace Envoy {
namespace Utils {
namespace {

// Helper function to set a key/value pair into Struct.
static void setKeyValue(::google::protobuf::Struct& data, std::string key,
                        std::string value) {
  (*data.mutable_fields())[key].set_string_value(value);
}

}  // namespace

void Authentication::SaveAuthAttributesToStruct(
    const istio::authn::Result& result, ::google::protobuf::Struct& data) {
  // TODO(diemvu): Refactor istio::authn::Result this conversion can be removed.
  if (!result.principal().empty()) {
    setKeyValue(data, istio::utils::AttributeName::kRequestAuthPrincipal,
                result.principal());
  }
  if (!result.peer_user().empty()) {
    // TODO(diemtvu): remove kSourceUser once migration to source.principal is
    // over. https://github.com/istio/istio/issues/4689
    setKeyValue(data, istio::utils::AttributeName::kSourceUser,
                result.peer_user());
    setKeyValue(data, istio::utils::AttributeName::kSourcePrincipal,
                result.peer_user());
  }
  if (result.has_origin()) {
    const auto& origin = result.origin();
    if (!origin.audiences().empty()) {
      // TODO(diemtvu): this should be send as repeated field once mixer
      // support string_list (https://github.com/istio/istio/issues/2802) For
      // now, just use the first value.
      setKeyValue(data, istio::utils::AttributeName::kRequestAuthAudiences,
                  origin.audiences(0));
    }
    if (!origin.presenter().empty()) {
      setKeyValue(data, istio::utils::AttributeName::kRequestAuthPresenter,
                  origin.presenter());
    }
    if (!origin.claims().fields().empty()) {
      auto s = (*data.mutable_fields())
                   [istio::utils::AttributeName::kRequestAuthClaims]
                       .mutable_struct_value()
                       ->mutable_fields();
      for (const auto& pair : origin.claims().fields()) {
        auto* value = (*s)[pair.first].mutable_list_value();
        value->CopyFrom(pair.second.list_value());
      }
    }
    if (!origin.raw_claims().empty()) {
      setKeyValue(data, istio::utils::AttributeName::kRequestAuthRawClaims,
                  origin.raw_claims());
    }
  }
}

const ProtobufWkt::Struct* Authentication::GetResultFromMetadata(
    const envoy::api::v2::core::Metadata& metadata) {
  const auto& iter =
      metadata.filter_metadata().find(Utils::IstioFilterName::kAuthentication);
  if (iter == metadata.filter_metadata().end()) {
    return nullptr;
  }
  return &(iter->second);
}

}  // namespace Utils
}  // namespace Envoy
