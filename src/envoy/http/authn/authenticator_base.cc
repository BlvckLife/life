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

#include "src/envoy/http/authn/authenticator_base.h"
#include "src/envoy/http/authn/filter_context.h"
#include "src/envoy/http/authn/mtls_authentication.h"
#include "src/envoy/utils/utils.h"

namespace iaapi = istio::authentication::v1alpha1;

namespace Envoy {
namespace Http {

AuthenticatorBase::AuthenticatorBase(
    FilterContext* filter_context,
    const AuthenticatorBase::DoneCallback& done_callback)
    : filter_context_(filter_context), done_callback_(done_callback) {}

AuthenticatorBase::~AuthenticatorBase() {}

void AuthenticatorBase::done(bool success) const { done_callback_(success); }

void AuthenticatorBase::validateX509(
    const iaapi::MutualTls&,
    const AuthenticatorBase::MethodDoneCallback& done_callback) const {
  // Boilerplate for x509 validation and extraction. This function should
  // extract user from SAN field from the x509 certificate come with request.
  // (validation might not be needed, as establisment of the connection by
  // itself is validation).
  // If x509 is missing (i.e connection is not on TLS) or SAN value is not
  // legit, call callback with status FAILED.
  ENVOY_LOG(debug, "AuthenticationFilter: {} this connection requires mTLS",
            __func__);
  MtlsAuthentication mtls_authn(filter_context_->connection());
  if (mtls_authn.IsMutualTLS() == false) {
    done_callback(nullptr, false);
    return;
  }

  std::unique_ptr<IstioAuthN::Payload> payload(new IstioAuthN::Payload());
  if (!mtls_authn.GetSourceUser(payload->mutable_x509()->mutable_user())) {
    done_callback(payload.get(), false);
  }

  // TODO (lei-tang): Adding other attributes (i.e ip) to payload if desire.
  done_callback(payload.get(), true);
}

void AuthenticatorBase::validateJwt(
    const iaapi::Jwt&,
    const AuthenticatorBase::MethodDoneCallback& done_callback) const {
  std::unique_ptr<IstioAuthN::Payload> payload(new IstioAuthN::Payload());
  // TODO (diemtvu/lei-tang): construct jwt_authenticator and call Verify;
  // pass done_callback so that it would be trigger by jwt_authenticator.onDone.
  done_callback(payload.get(), false);
}

namespace {
// Returns true if rule is mathed for peer_id
bool isRuleMatchedWithPeer(const iaapi::CredentialRule& rule,
                           const std::string& peer_id) {
  if (rule.matching_peers_size() == 0) {
    return true;
  }
  for (const auto& allowed_id : rule.matching_peers()) {
    if (peer_id == allowed_id) {
      return true;
    }
  }
  return false;
}
}  // namespace

const istio::authentication::v1alpha1::CredentialRule&
findCredentialRuleOrDefault(
    const istio::authentication::v1alpha1::Policy& policy,
    const std::string& peer_id) {
  for (const auto& rule : policy.credential_rules()) {
    if (isRuleMatchedWithPeer(rule, peer_id)) {
      return rule;
    }
  }
  return iaapi::CredentialRule::default_instance();
}

}  // namespace Http
}  // namespace Envoy
