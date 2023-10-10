// Copyright (c) 2014-2023 Zano Project
// Copyright (c) 2014-2018 The Louisdor Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chaingen.h"
#include "ionic_swap_tests.h"
#include "wallet_test_core_proxy.h"

#include "random_helper.h"
#include "tx_builder.h"


ionic_swap_basic_test::ionic_swap_basic_test()
{
  REGISTER_CALLBACK_METHOD(ionic_swap_basic_test, configure_core);
  REGISTER_CALLBACK_METHOD(ionic_swap_basic_test, c1);

  m_hardforks.set_hardfork_height(1, 1);
  m_hardforks.set_hardfork_height(2, 1);
  m_hardforks.set_hardfork_height(3, 1);
  m_hardforks.set_hardfork_height(4, 2);
}

bool ionic_swap_basic_test::generate(std::vector<test_event_entry>& events) const
{
  uint64_t ts = test_core_time::get_time();
  m_accounts.resize(TOTAL_ACCS_COUNT);
  currency::account_base& miner_acc = m_accounts[MINER_ACC_IDX]; miner_acc.generate(); miner_acc.set_createtime(ts);
  currency::account_base& alice_acc = m_accounts[ALICE_ACC_IDX]; alice_acc.generate(); alice_acc.set_createtime(ts);
  currency::account_base& bob_acc =   m_accounts[BOB_ACC_IDX];   bob_acc.generate();   bob_acc.set_createtime(ts);
  //account_base& carol_acc = m_accounts[CAROL_ACC_IDX]; carol_acc.generate(); carol_acc.set_createtime(ts);

  MAKE_GENESIS_BLOCK(events, blk_0, miner_acc, ts);
  DO_CALLBACK(events, "configure_core"); // default configure_core callback will initialize core runtime config with m_hardforks
  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_acc, CURRENCY_MINED_MONEY_UNLOCK_WINDOW + 3);

  DO_CALLBACK(events, "c1");

  return true;
}

bool ionic_swap_basic_test::c1(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  bool r = false;
  std::shared_ptr<tools::wallet2> miner_wlt = init_playtime_test_wallet(events, c, MINER_ACC_IDX);
  std::shared_ptr<tools::wallet2> alice_wlt = init_playtime_test_wallet(events, c, ALICE_ACC_IDX);
  std::shared_ptr<tools::wallet2> bob_wlt   = init_playtime_test_wallet(events, c, BOB_ACC_IDX);

  // check passing over the hardfork
  CHECK_AND_ASSERT_MES(c.get_blockchain_storage().is_hardfork_active(ZANO_HARDFORK_04_ZARCANUM), false, "ZANO_HARDFORK_04_ZARCANUM is active");

  miner_wlt->refresh();

  currency::asset_descriptor_base adb = AUTO_VAL_INIT(adb);
  adb.total_max_supply = 1000000 * COIN; //1M coins
  adb.full_name = "Test coins";
  adb.ticker = "TCT";
  adb.decimal_point = 12;

  uint64_t alice_amount = adb.total_max_supply / 2;
  uint64_t bob_amount = adb.total_max_supply / 2;

  std::vector<currency::tx_destination_entry> destinations(4);
  destinations[0].addr.push_back(m_accounts[ALICE_ACC_IDX].get_public_address());
  destinations[0].amount = alice_amount;
  destinations[0].asset_id = currency::null_pkey;
  destinations[1].addr.push_back(m_accounts[BOB_ACC_IDX].get_public_address());
  destinations[1].amount = bob_amount;
  destinations[1].asset_id = currency::null_pkey;

  destinations[2] = currency::tx_destination_entry(COIN, m_accounts[ALICE_ACC_IDX].get_public_address());
  destinations[3] = currency::tx_destination_entry(COIN, m_accounts[BOB_ACC_IDX].get_public_address());

  LOG_PRINT_MAGENTA("destinations[0].asset_id:" << destinations[0].asset_id, LOG_LEVEL_0);
  LOG_PRINT_MAGENTA("destinations[1].asset_id:" << destinations[1].asset_id, LOG_LEVEL_0);
  LOG_PRINT_MAGENTA("currency::null_pkey:" << currency::null_pkey, LOG_LEVEL_0);

  currency::transaction tx = AUTO_VAL_INIT(tx);
  crypto::public_key asset_id = currency::null_pkey;
  miner_wlt->deploy_new_asset(adb, destinations, tx, asset_id);
  LOG_PRINT_L0("Deployed new asset: " << asset_id << ", tx_id: " << currency::get_transaction_hash(tx));

  //currency::transaction res_tx = AUTO_VAL_INIT(res_tx);
  //miner_wlt->transfer(COIN, alice_wlt->get_account().get_public_address(), res_tx);
  //miner_wlt->transfer(COIN, bob_wlt->get_account().get_public_address(), res_tx);

  r = mine_next_pow_blocks_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_blocks_in_playtime failed");


  bob_wlt->refresh();
  alice_wlt->refresh();
  uint64_t mined = 0;
  std::unordered_map<crypto::public_key, tools::wallet_public::asset_balance_entry_base> balances;
  bob_wlt->balance(balances, mined);

  auto it_asset = balances.find(asset_id);
  auto it_native = balances.find(currency::native_coin_asset_id);


  CHECK_AND_ASSERT_MES(it_asset != balances.end() && it_native != balances.end(), false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_asset->second.total == bob_amount, false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_native->second.total == COIN, false, "Failed to find needed asset in result balances");

  balances.clear();
  alice_wlt->balance(balances, mined);

  it_asset = balances.find(asset_id);
  it_native = balances.find(currency::native_coin_asset_id);

  CHECK_AND_ASSERT_MES(it_asset != balances.end() && it_native != balances.end(), false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_native->second.total == COIN, false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_asset->second.total == alice_amount, false, "Failed to find needed asset in result balances");

  const uint64_t assets_to_exchange = 10 * COIN;
  const uint64_t native_coins_to_exchange = COIN/2;

  {
    // Alice wants to trade with Bob, to exchange 10.0 TCT to 0.5 ZANO 
    view::ionic_swap_proposal_info proposal_details = AUTO_VAL_INIT(proposal_details);
    proposal_details.fee_paid_by_a = TESTS_DEFAULT_FEE;
    proposal_details.mixins = 10;
    proposal_details.to_finalizer.push_back(view::asset_funds{ asset_id , assets_to_exchange });
    proposal_details.to_initiator.push_back(view::asset_funds{ currency::native_coin_asset_id , native_coins_to_exchange });

    tools::wallet_public::ionic_swap_proposal proposal = AUTO_VAL_INIT(proposal);
    alice_wlt->create_ionic_swap_proposal(proposal_details, m_accounts[BOB_ACC_IDX].get_public_address(), proposal);

    view::ionic_swap_proposal_info proposal_decoded_info = AUTO_VAL_INIT(proposal_decoded_info);
    bob_wlt->get_ionic_swap_proposal_info(proposal, proposal_decoded_info);

    //Validate proposal
    if (proposal_decoded_info.to_finalizer != proposal_details.to_finalizer
      || proposal_decoded_info.to_initiator != proposal_details.to_initiator
      || proposal_decoded_info.fee_paid_by_a != proposal_details.fee_paid_by_a
      || proposal_decoded_info.mixins != proposal_details.mixins
      )
    {
      CHECK_AND_ASSERT_MES(false, false, "proposal actual and proposals decoded mismatch");
    }

    currency::transaction res_tx2 = AUTO_VAL_INIT(res_tx2);
    r = bob_wlt->accept_ionic_swap_proposal(proposal, res_tx2);
    CHECK_AND_ASSERT_MES(r, false, "Failed to accept ionic proposal");
  }

  r = mine_next_pow_blocks_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_blocks_in_playtime failed");


  alice_wlt->refresh();
  balances.clear();
  alice_wlt->balance(balances, mined);
  it_asset = balances.find(asset_id);
  it_native = balances.find(currency::native_coin_asset_id);

  CHECK_AND_ASSERT_MES(it_asset != balances.end(), false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_native->second.total == native_coins_to_exchange + COIN - TESTS_DEFAULT_FEE, false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_asset->second.total == alice_amount - assets_to_exchange, false, "Failed to find needed asset in result balances");


  bob_wlt->refresh();
  balances.clear();
  bob_wlt->balance(balances, mined);
  it_asset = balances.find(asset_id);
  it_native = balances.find(currency::native_coin_asset_id);

  CHECK_AND_ASSERT_MES(it_asset != balances.end(), false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_native->second.total == COIN - native_coins_to_exchange, false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_asset->second.total == bob_amount + assets_to_exchange, false, "Failed to find needed asset in result balances");

  {
    //now Alice want to trade with Bob, to send 0.5 ZANO and get 10.0 TCT in exchange
    view::ionic_swap_proposal_info proposal_details = AUTO_VAL_INIT(proposal_details);
    proposal_details.fee_paid_by_a = TESTS_DEFAULT_FEE;
    proposal_details.mixins = 10;
    proposal_details.to_finalizer.push_back(view::asset_funds{ currency::native_coin_asset_id , native_coins_to_exchange });
    proposal_details.to_initiator.push_back(view::asset_funds{ asset_id , assets_to_exchange });

    tools::wallet_public::ionic_swap_proposal proposal = AUTO_VAL_INIT(proposal);
    alice_wlt->create_ionic_swap_proposal(proposal_details, m_accounts[BOB_ACC_IDX].get_public_address(), proposal);

    view::ionic_swap_proposal_info proposal_decoded_info = AUTO_VAL_INIT(proposal_decoded_info);
    bob_wlt->get_ionic_swap_proposal_info(proposal, proposal_decoded_info);

    //Validate proposal
    if (proposal_decoded_info.to_finalizer != proposal_details.to_finalizer
      || proposal_decoded_info.to_initiator != proposal_details.to_initiator
      || proposal_decoded_info.fee_paid_by_a != proposal_details.fee_paid_by_a
      || proposal_decoded_info.mixins != proposal_details.mixins
      )
    {
      CHECK_AND_ASSERT_MES(false, false, "proposal actual and proposals decoded mismatch");
    }

    currency::transaction res_tx2 = AUTO_VAL_INIT(res_tx2);
    r = bob_wlt->accept_ionic_swap_proposal(proposal, res_tx2);
    CHECK_AND_ASSERT_MES(r, false, "Failed to accept ionic proposal");
  }

  r = mine_next_pow_blocks_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_blocks_in_playtime failed");


  alice_wlt->refresh();
  balances.clear();
  alice_wlt->balance(balances, mined);
  it_asset = balances.find(asset_id);
  it_native = balances.find(currency::native_coin_asset_id);

  CHECK_AND_ASSERT_MES(it_asset != balances.end(), false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_native->second.total == native_coins_to_exchange + COIN - TESTS_DEFAULT_FEE * 2 - native_coins_to_exchange, false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_asset->second.total == alice_amount, false, "Failed to find needed asset in result balances");


  bob_wlt->refresh();
  balances.clear();
  bob_wlt->balance(balances, mined);
  it_asset = balances.find(asset_id);
  it_native = balances.find(currency::native_coin_asset_id);

  CHECK_AND_ASSERT_MES(it_asset != balances.end(), false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_native->second.total == COIN - native_coins_to_exchange + native_coins_to_exchange, false, "Failed to find needed asset in result balances");
  CHECK_AND_ASSERT_MES(it_asset->second.total == bob_amount, false, "Failed to find needed asset in result balances");





  //TODO: 
  // add fee paid by bob scenario
  // add transfer of tokens without native coins
  // different fail combination
  return true;
}

