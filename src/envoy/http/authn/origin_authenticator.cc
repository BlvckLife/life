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

#include "src/envoy/http/authn/origin_authenticator.h"
#include "authentication/v1alpha1/policy.pb.h"

namespace iaapi = istio::authentication::v1alpha1;

namespace Envoy {
namespace Http {
namespace Istio {
namespace AuthN {

OriginAuthenticator::OriginAuthenticator(
    FilterContext* filter_context, const DoneCallback& done_callback,
    JwtToAuthStoreMap& jwt_store, const iaapi::CredentialRule& credential_rule)
    : AuthenticatorBase(filter_context, done_callback, jwt_store),
      credential_rule_(credential_rule) {}

void OriginAuthenticator::run() {
  if (credential_rule_.origins_size() == 0) {
    switch (credential_rule_.binding()) {
      case iaapi::CredentialRule::USE_ORIGIN:
        // Validation should reject policy that have rule to USE_ORIGIN but
        // does not provide any origin method so this code should
        // never reach. However, it's ok to treat it as authentication
        // fails.
        ENVOY_LOG(warn,
                  "Principal is binded to origin, but not methods specified in "
                  "rule {}",
                  credential_rule_.DebugString());
        onMethodDone(nullptr, false);
        break;
      case iaapi::CredentialRule::USE_PEER:
        // On the other hand, it's ok to have no (origin) methods if
        // rule USE_SOURCE
        onMethodDone(nullptr, true);
        break;
      default:
        // Should never come here.
        ENVOY_LOG(error, "Invalid binding value for rule {}",
                  credential_rule_.DebugString());
        break;
    }
    return;
  }
  // Note to explain the change (the note will be removed after the code review)
  // The callback variable in runMethod() is a temporary variable and will be
  // de-allocated after runMethod() returns.
  // When the callback is later invoked on the reference of the temporary
  // callback variable, it causes segmentation fault.
  //
  //  runMethod(credential_rule_.origins(0), [this](const Payload* payload,
  //                                                bool success) {
  //    onMethodDone(payload, success);
  //  });
  // Changed into the following:
  callbackForRunMethod.reset(new AuthenticatorBase::MethodDoneCallback(
      [this](const Payload* payload, bool success) {
        onMethodDone(payload, success);
      }));
  runMethod(credential_rule_.origins(0), *callbackForRunMethod.get());
}

void OriginAuthenticator::runMethod(
    const istio::authentication::v1alpha1::OriginAuthenticationMethod& method,
    const AuthenticatorBase::MethodDoneCallback& callback) {
  validateJwt(method.jwt(), callback);
}

void OriginAuthenticator::onMethodDone(const Payload* payload, bool success) {
  if (!success && method_index_ + 1 < credential_rule_.origins_size()) {
    ENVOY_LOG(debug, "{}: success is false, method_index is {}", __FUNCTION__,
              method_index_);
    // Authentication fail, try the next method, if available.
    method_index_++;
    runMethod(credential_rule_.origins(method_index_),
              [this](const Payload* payload, bool success) {
                onMethodDone(payload, success);
              });
    return;
  }

  if (success) {
    ENVOY_LOG(debug, "{}: success is true, method_index is {}", __FUNCTION__,
              method_index_);
    filter_context()->setOriginResult(payload);
    filter_context()->setPrincipal(credential_rule_.binding());
  }
  done(success);
}

}  // namespace AuthN
}  // namespace Istio
}  // namespace Http
}  // namespace Envoy
