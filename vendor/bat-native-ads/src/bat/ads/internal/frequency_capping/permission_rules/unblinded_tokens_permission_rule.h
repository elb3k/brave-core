/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_VENDOR_BAT_NATIVE_ADS_SRC_BAT_ADS_INTERNAL_FREQUENCY_CAPPING_PERMISSION_RULES_UNBLINDED_TOKENS_PERMISSION_RULE_H_
#define BRAVE_VENDOR_BAT_NATIVE_ADS_SRC_BAT_ADS_INTERNAL_FREQUENCY_CAPPING_PERMISSION_RULES_UNBLINDED_TOKENS_PERMISSION_RULE_H_

#include <string>

#include "bat/ads/internal/frequency_capping/permission_rules/permission_rule.h"

namespace ads {

class UnblindedTokensPermissionRule final : public PermissionRule {
 public:
  UnblindedTokensPermissionRule();
  ~UnblindedTokensPermissionRule() override;

  bool ShouldAllow() override;

  std::string GetLastMessage() const override;

 private:
  bool DoesRespectCap();

  UnblindedTokensPermissionRule(const UnblindedTokensPermissionRule&) = delete;
  UnblindedTokensPermissionRule& operator=(
      const UnblindedTokensPermissionRule&) = delete;

  std::string last_message_;
};

}  // namespace ads

#endif  // BRAVE_VENDOR_BAT_NATIVE_ADS_SRC_BAT_ADS_INTERNAL_FREQUENCY_CAPPING_PERMISSION_RULES_UNBLINDED_TOKENS_PERMISSION_RULE_H_
