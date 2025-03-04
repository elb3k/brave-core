/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/account/account.h"

#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "brave/components/brave_ads/core/internal/account/confirmations/confirmation_info.h"
#include "brave/components/brave_ads/core/internal/account/confirmations/confirmations.h"
#include "brave/components/brave_ads/core/internal/account/deposits/deposits_factory.h"
#include "brave/components/brave_ads/core/internal/account/statement/statement.h"
#include "brave/components/brave_ads/core/internal/account/tokens/token_generator_interface.h"
#include "brave/components/brave_ads/core/internal/account/transactions/transaction_info.h"
#include "brave/components/brave_ads/core/internal/account/transactions/transactions.h"
#include "brave/components/brave_ads/core/internal/account/user_rewards/user_rewards.h"
#include "brave/components/brave_ads/core/internal/account/wallet/wallet_util.h"
#include "brave/components/brave_ads/core/internal/ads_notifier_manager.h"
#include "brave/components/brave_ads/core/internal/client/ads_client_util.h"
#include "brave/components/brave_ads/core/internal/common/logging_util.h"
#include "brave/components/brave_ads/core/internal/settings/settings.h"
#include "brave/components/brave_ads/core/mojom/brave_ads.mojom.h"  // IWYU pragma: keep
#include "brave/components/brave_ads/core/public/ad_units/ad_type.h"
#include "brave/components/brave_rewards/common/pref_names.h"

namespace brave_ads {

Account::Account(TokenGeneratorInterface* token_generator)
    : token_generator_(token_generator) {
  CHECK(token_generator_);

  AddAdsClientNotifierObserver(this);

  InitializeConfirmations();
}

Account::~Account() {
  RemoveAdsClientNotifierObserver(this);
}

void Account::AddObserver(AccountObserver* observer) {
  CHECK(observer);
  observers_.AddObserver(observer);
}

void Account::RemoveObserver(AccountObserver* observer) {
  CHECK(observer);
  observers_.RemoveObserver(observer);
}

void Account::SetWallet(const std::string& payment_id,
                        const std::string& recovery_seed) {
  const std::optional<WalletInfo> wallet = ToWallet(payment_id, recovery_seed);
  if (!wallet) {
    BLOG(0, "Failed to initialize wallet");
    return NotifyFailedToInitializeWallet();
  }

  wallet_ = wallet;

  BLOG(1, "Successfully initialized wallet");
  NotifyDidInitializeWallet(*wallet);
}

// static
void Account::GetStatement(GetStatementOfAccountsCallback callback) {
  if (!UserHasJoinedBraveRewards()) {
    return std::move(callback).Run(/*statement*/ nullptr);
  }

  return BuildStatement(std::move(callback));
}

void Account::Deposit(const std::string& creative_instance_id,
                      const std::string& segment,
                      const AdType ad_type,
                      const ConfirmationType confirmation_type) const {
  CHECK(!creative_instance_id.empty());
  CHECK_NE(AdType::kUndefined, ad_type);
  CHECK_NE(ConfirmationType::kUndefined, confirmation_type);

  const std::unique_ptr<DepositInterface> deposit =
      DepositsFactory::Build(confirmation_type);
  if (!deposit) {
    return;
  }

  deposit->GetValue(
      creative_instance_id,
      base::BindOnce(&Account::DepositCallback, weak_factory_.GetWeakPtr(),
                     creative_instance_id, segment, ad_type,
                     confirmation_type));
}

///////////////////////////////////////////////////////////////////////////////

void Account::DepositCallback(const std::string& creative_instance_id,
                              const std::string& segment,
                              const AdType ad_type,
                              const ConfirmationType confirmation_type,
                              const bool success,
                              const double value) const {
  if (!success) {
    return FailedToProcessDeposit(creative_instance_id, ad_type,
                                  confirmation_type);
  }

  ProcessDeposit(creative_instance_id, segment, value, ad_type,
                 confirmation_type);
}

void Account::ProcessDeposit(const std::string& creative_instance_id,
                             const std::string& segment,
                             const double value,
                             const AdType ad_type,
                             const ConfirmationType confirmation_type) const {
  if (!UserHasJoinedBraveRewards()) {
    return SuccessfullyProcessedDeposit(BuildTransaction(
        creative_instance_id, segment, value, ad_type, confirmation_type));
  }

  AddTransaction(
      creative_instance_id, segment, value, ad_type, confirmation_type,
      base::BindOnce(&Account::ProcessDepositCallback,
                     weak_factory_.GetWeakPtr(), creative_instance_id, ad_type,
                     confirmation_type));
}

void Account::ProcessDepositCallback(const std::string& creative_instance_id,
                                     const AdType ad_type,
                                     const ConfirmationType confirmation_type,
                                     const bool success,
                                     const TransactionInfo& transaction) const {
  if (!success) {
    return FailedToProcessDeposit(creative_instance_id, ad_type,
                                  confirmation_type);
  }

  SuccessfullyProcessedDeposit(transaction);
}

void Account::SuccessfullyProcessedDeposit(
    const TransactionInfo& transaction) const {
  BLOG(3, "Successfully processed deposit for "
              << transaction.ad_type << " with creative instance id "
              << transaction.creative_instance_id << " and "
              << transaction.confirmation_type << " valued at "
              << transaction.value);

  confirmations_->Confirm(transaction);

  NotifyDidProcessDeposit(transaction);

  AdsNotifierManager::GetInstance().NotifyAdRewardsDidChange();
}

void Account::FailedToProcessDeposit(
    const std::string& creative_instance_id,
    const AdType ad_type,
    const ConfirmationType confirmation_type) const {
  BLOG(0, "Failed to process deposit for "
              << ad_type << " with creative instance id "
              << creative_instance_id << " and " << confirmation_type);

  NotifyFailedToProcessDeposit(creative_instance_id, ad_type,
                               confirmation_type);
}

void Account::Initialize() {
  MaybeInitializeUserRewards();

  AdsNotifierManager::GetInstance().NotifyAdRewardsDidChange();
}

void Account::InitializeConfirmations() {
  BLOG(1, "Initialize confirmations");

  confirmations_ = std::make_unique<Confirmations>(token_generator_);
  confirmations_->SetDelegate(this);
}

void Account::MaybeInitializeUserRewards() {
  if (!wallet_) {
    return;
  }

  if (user_rewards_ || !UserHasJoinedBraveRewards()) {
    return;
  }

  BLOG(1, "Initialize user rewards");

  // We do not need to destroy the `user_rewards` object when a user resets
  // Brave Rewards because the associated data and the `Ads` instance will be
  // destroyed.

  user_rewards_ = std::make_unique<UserRewards>(token_generator_, *wallet_);
  user_rewards_->SetDelegate(this);

  user_rewards_->FetchIssuers();

  user_rewards_->MaybeRedeemPaymentTokens();
}

void Account::MaybeRefillConfirmationTokens() {
  if (user_rewards_) {
    user_rewards_->MaybeRefillConfirmationTokens();
  }
}

void Account::NotifyDidInitializeWallet(const WalletInfo& wallet) const {
  for (AccountObserver& observer : observers_) {
    observer.OnDidInitializeWallet(wallet);
  }
}

void Account::NotifyFailedToInitializeWallet() const {
  for (AccountObserver& observer : observers_) {
    observer.OnFailedToInitializeWallet();
  }
}

void Account::NotifyDidProcessDeposit(
    const TransactionInfo& transaction) const {
  for (AccountObserver& observer : observers_) {
    observer.OnDidProcessDeposit(transaction);
  }
}

void Account::NotifyFailedToProcessDeposit(
    const std::string& creative_instance_id,
    const AdType ad_type,
    const ConfirmationType confirmation_type) const {
  for (AccountObserver& observer : observers_) {
    observer.OnFailedToProcessDeposit(creative_instance_id, ad_type,
                                      confirmation_type);
  }
}

void Account::OnNotifyDidInitializeAds() {
  Initialize();
}

void Account::OnNotifyPrefDidChange(const std::string& path) {
  if (path == brave_rewards::prefs::kEnabled) {
    Initialize();
  }
}

void Account::OnNotifyRewardsWalletDidUpdate(const std::string& payment_id,
                                             const std::string& recovery_seed) {
  SetWallet(payment_id, recovery_seed);

  Initialize();
}

void Account::OnDidConfirm(const ConfirmationInfo& /*confirmation*/) {
  MaybeRefillConfirmationTokens();
}

void Account::OnFailedToConfirm(const ConfirmationInfo& /*confirmation*/) {
  MaybeRefillConfirmationTokens();
}

void Account::OnDidMigrateVerifiedRewardsUser() {
  InitializeConfirmations();
}

}  // namespace brave_ads
