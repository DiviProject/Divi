#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests running of masternodes with a vault block producer, querying their status (as well as
# updates to the status, e.g. if a node goes offline) and the
# payouts to them.

from util import *
from masternode import *

import collections
import time


class MnPlusVaults (MnTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime"]
    self.number_of_nodes = 7

  def setup_network (self, config_line=None, extra_args=[]):
    all_args = [self.base_args]*self.number_of_nodes
    all_args[6].append("-vault=1")
    all_args[6].append("-spendzeroconfchange")
    all_args[6].append("-autocombine=0")
    self.nodes = [
      start_node (i, self.options.tmpdir, extra_args=all_args[i])
      for i in range(self.number_of_nodes)
    ]
    self.setup = [None]*self.number_of_nodes

    self.time = 1580000000
    assert self.time < time.time ()
    set_node_times (self.nodes, self.time)
    connect_nodes (self.nodes[3], 4)
    connect_nodes (self.nodes[3], 5)
    connect_nodes (self.nodes[3], 6)
    connect_nodes (self.nodes[4], 5)
    connect_nodes (self.nodes[4], 6)
    connect_nodes (self.nodes[5], 6)
    connect_nodes (self.nodes[0], 3)
    connect_nodes (self.nodes[0], 1)
    connect_nodes (self.nodes[0], 2)

    self.is_network_split = False

  def stop_node (self, n):
    """Stops node n (0..2)."""

    stop_node (self.nodes[n], n)
    self.nodes[n] = None

  def advance_time (self, dt=1,delay=None):
    """Advances mocktime by the given number of seconds."""

    self.time += dt
    set_node_times (self.nodes, self.time)
    if delay is not None:
      time.sleep(delay)

  def mine_blocks (self, n):
    """Mines blocks with node 3."""
    if self.nodes[3].getblockcount() > 100:
      self.nodes[6].setgenerate(n)
    else:
      self.nodes[3].setgenerate( n)
    sync_blocks (self.nodes)

  def fund_vaults(self):
    staking_vault = self.nodes[6]
    staking_address = staking_vault.getnewaddress()
    funding_data = []
    funding_format = {staking_address:{"amount":20.0,"repetitions":20}}
    funding_data.append(self.nodes[0].fundvault(funding_format))

    sync_mempools(self.nodes)
    self.mine_blocks(1)
    for data in funding_data:
      for vault in data["vault"]:
        staking_vault.addvault(vault["encoding"],data["txhash"])


  def fund_masternodes (self):
    print ("Funding masternodes...")
    # The collateral needs 15 confirmations, and the masternode broadcast
    # signature must be later than that block's timestamp.  Thus we start
    # with a very early timestamp.
    set_node_times (self.nodes, self.time)

    self.nodes[0].setgenerate ( 5)
    sync_blocks (self.nodes)
    self.mine_blocks (24)
    self.fund_vaults()
    self.mine_blocks(1)
    assert_near (self.nodes[0].getbalance (), 6250,1e-3)

    self.setup_masternode(0,1,"mn1","copper")
    self.setup_masternode(0,2,"mn2","silver")
    self.mine_blocks (15)
    set_node_times (self.nodes, self.time)
    self.fund_vaults()
    self.mine_blocks (1)
    assert_equal(self.nodes[3].getblockcount(),48)
    for _ in range(53):
      self.mock_wait(duration=1)
      self.mine_blocks(1)

  def start_masternodes (self):
    print ("Starting masternodes...")
    self.stop_masternode_daemons()
    self.start_masternode_daemons()
    self.connect_masternodes_to_peers([3, 4, 5, 6],updateMockTime=True)

    # The masternodes will be inactive until activation.
    sync_blocks(self.nodes)
    # The masternodes will be inactive until activation.
    assert_equal (self.nodes[3].listmasternodes (), [])
    self.check_masternodes_are_locally_inactive()
    # Activate the masternodes.  We do not need to keep the
    # cold node online.
    self.mock_wait(100)
    assert_equal(self.broadcast_with_ping_update("mn1")["status"],"success")
    assert_equal(self.broadcast_with_ping_update("mn2")["status"],"success")

    # Check status of the masternodes themselves.
    self.wait_for_masternodes_to_be_locally_active(updateMockTime=True)

    # Check list of masternodes on node 3.
    lst = self.wait_for_mn_list_to_sync(self.nodes[3],2)
    self.check_list_for_all_masternodes(lst)
    stop_node(self.nodes[0],0)
    self.nodes[0]=None

  def verify_number_of_votes_exist_and_tally_winners(self,startBlockHeight, endBlockHeight, expected_votes, expected_address = None):
    heightSet = set()
    winnerTally = collections.Counter ()
    expectedNumberOfBlocks = endBlockHeight - startBlockHeight+1
    for nodeId in [3,4,5,6]:
      winnerListsByNode = self.nodes[nodeId].getmasternodewinners (str (expectedNumberOfBlocks))
      for winner in winnerListsByNode:
        if winner["nHeight"] < startBlockHeight or winner["nHeight"] > endBlockHeight:
          continue
        if expected_address is not None:
          if winner["winner"]["address"] != expected_address:
            continue
        isInsertable = winner["nHeight"] not in heightSet
        if winner["winner"]["nVotes"] == expected_votes and isInsertable:
          heightSet.add(winner["nHeight"])
          winnerTally[winner["winner"]["address"]] += 1
        if len(heightSet) == expectedNumberOfBlocks:
          break
      if len(heightSet) == expectedNumberOfBlocks:
        break

    for height in range(startBlockHeight, endBlockHeight+1):
      try:
        heightSet.remove(height)
      except:
        assert_equal("Height without enough votes (on ANY of the 4 helper nodes) found: height {}".format(height),"")

    return winnerTally


  def payments_both_active (self):
    print ("Masternode payments with both active...")

    # For payments to be successful, the masternodes need to be active
    # at least 8000 seconds and we need more than 100 blocks.
    self.mine_blocks (100)
    for _ in range (100):
      self.advance_time (100)
      time.sleep(0.01)

    cnt = self.nodes[3].getmasternodecount ()
    assert_equal (cnt["total"], 2)
    assert_equal (cnt["enabled"], 2)
    assert_equal (cnt["inqueue"], 2)

    # Mine some blocks, but advance the time in between and do it
    # one by one so the masternode winners can get broadcast between
    # blocks and such.
    startHeight = self.nodes[3].getblockcount () + 11
    for _ in range (50):
      self.mine_blocks (1)
      self.advance_time (10)
    endHeight = self.nodes[3].getblockcount ()

    # Check the masternode winners for those 50 blocks.  It should be
    # our two masternodes, randomly, with two votes each.
    # In case of an out-of-sync message we need to fall back to another node
    winners = self.verify_number_of_votes_exist_and_tally_winners(startHeight,endHeight, 2)

    addr1 = self.nodes[1].getmasternodestatus ()["addr"]
    addr2 = self.nodes[2].getmasternodestatus ()["addr"]
    assert_equal (len (winners), 2)
    assert_greater_than (winners[addr1], 0)
    assert_greater_than (winners[addr2], 0)

    # On average, addr2 would win twice as much as addr1 but this
    # test runs a single instance so an assertion will fail with
    # a relatively high probability.
    total_wins = winners[addr1]+winners[addr2]
    minimum_expected_wins_for_addr2 = 0.50 * total_wins
    assert_greater_than (winners[addr2], minimum_expected_wins_for_addr2)
    assert_greater_than (minimum_expected_wins_for_addr2, winners[addr1])

  def payments_one_active (self):
    print ("Masternode payments with one active...")

    # Shut down one of the masternodes and advance the time so it will
    # be seen as disabled in the network.
    self.stop_node (2)
    for _ in range (100):
      self.advance_time (120)
      time.sleep(0.01)

    lst = self.nodes[3].listmasternodes ()
    assert_equal (len (lst), 1)
    assert_equal (lst[0]["tier"], "COPPER")
    assert_equal (lst[0]["status"], "ENABLED")
    assert_equal (lst[0]["txhash"], self.setup[1].cfg.txid)

    # Generate more blocks.  Payments to the disabled node will
    # stop at least for the blocks for which we do fresh broadcasts.
    startHeight = self.nodes[3].getblockcount () + 11
    for _ in range (50):
      self.mine_blocks (1)
      self.advance_time (10)
    endHeight = self.nodes[3].getblockcount ()

    addr = self.nodes[1].getmasternodestatus ()["addr"]
    self.verify_number_of_votes_exist_and_tally_winners(startHeight,endHeight,1,addr)

  def run_test (self):
    self.fund_masternodes ()
    self.start_masternodes ()
    self.payments_both_active ()
    self.payments_one_active ()

if __name__ == '__main__':
  MnPlusVaults ().main ()
