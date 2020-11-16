#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests running of masternodes, querying their status (as well as
# updates to the status, e.g. if a node goes offline) and the
# payouts to them.
#
# We use seven nodes:
# - node 0 is used to fund two masternodes but then goes offline
# - node 1 is a copper masternode
# - node 2 is a silver masternode
# - node 3 is used to mine blocks and check the masternode states
# - nodes 4-6 are just used to get above the "three full nodes" threshold
#   (together with node 3, independent of the masternodes online)

from test_framework import BitcoinTestFramework
from util import *
from masternode import *

import collections
import time


class MnStatusTest (BitcoinTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug"]

  def setup_chain (self):
    for i in range (7):
      initialize_datadir (self.options.tmpdir, i)

  def setup_network (self, config_line=None, extra_args=[]):

    # Initially we just start the funding and mining nodes
    # and use them to set up the masternodes.
    self.nodes = [
      start_node (0, self.options.tmpdir, extra_args=self.base_args),
      None,
      None,
    ] + [
      start_node (i, self.options.tmpdir, extra_args=self.base_args)
      for i in [3, 4, 5, 6]
    ]

    # We want to work with mock times that are beyond the genesis
    # block timestamp but before current time (so that nodes being
    # started up and before they get on mocktime aren't rejecting
    # the on-disk blockchain).
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

    self.is_network_split = False

  def start_node (self, n):
    """Starts node n (0..2) with the proper arguments
    and masternode config for it."""

    configs = [
      [c.line for c in self.cfg],
      [self.cfg[0].line],
      [self.cfg[1].line],
    ]

    args = self.base_args[:]
    if n == 1 or n == 2:
      args.append ("-masternode")
      args.append ("-masternodeprivkey=%s" % self.cfg[n - 1].privkey)

    self.nodes[n] = start_node (n, self.options.tmpdir,
                                extra_args=args, mn_config_lines=configs[n])
    self.nodes[n].setmocktime (self.time)

    for i in [3, 4, 5, 6]:
      connect_nodes (self.nodes[n], i)
    sync_blocks (self.nodes)

  def stop_node (self, n):
    """Stops node n (0..2)."""

    stop_node (self.nodes[n], n)
    self.nodes[n] = None

  def advance_time (self, dt=1):
    """Advances mocktime by the given number of seconds."""

    self.time += dt
    set_node_times (self.nodes, self.time)

  def mine_blocks (self, n):
    """Mines blocks with node 3."""

    self.nodes[3].setgenerate(True, n)
    sync_blocks (self.nodes)

  def run_test (self):
    self.fund_masternodes ()
    self.start_masternodes ()
    self.sync_masternodes ()
    self.payments_both_active ()
    self.payments_one_active ()
    self.check_rewards ()

  def fund_masternodes (self):
    print ("Funding masternodes...")

    # The collateral needs 15 confirmations, and the masternode broadcast
    # signature must be later than that block's timestamp.  Thus we start
    # with a very early timestamp.
    genesis = self.nodes[0].getblockhash (0)
    genesisTime = self.nodes[0].getblockheader (genesis)["time"]
    assert genesisTime < self.time
    set_node_times (self.nodes, genesisTime)

    self.nodes[0].setgenerate (True, 5)
    sync_blocks (self.nodes)
    self.mine_blocks (25)
    assert_equal (self.nodes[0].getbalance (), 6250)

    id1 = self.nodes[0].allocatefunds ("masternode", "mn1", "copper")["txhash"]
    id2 = self.nodes[0].allocatefunds ("masternode", "mn2", "silver")["txhash"]
    sync_mempools (self.nodes)
    self.mine_blocks (15)
    set_node_times (self.nodes, self.time)
    self.mine_blocks (1)

    self.cfg = [
      fund_masternode (self.nodes[0], "mn1", "copper", id1, "localhost:%d" % p2p_port (1)),
      fund_masternode (self.nodes[0], "mn2", "silver", id2, "localhost:%d" % p2p_port (2)),
    ]

  def start_masternodes (self):
    print ("Starting masternodes...")

    self.stop_node (0)
    for i in range (3):
      self.start_node (i)

    # The masternodes will be inactive until activation.
    time.sleep (0.1)
    assert_equal (self.nodes[3].listmasternodes (), [])
    for i in [1, 2]:
      assert_raises (JSONRPCException, self.nodes[i].getmasternodestatus)

    # Activate the masternodes.  We do not need to keep the
    # cold node online.
    self.nodes[0].startmasternode ("mn1")
    time.sleep (0.1)
    broadcast = self.nodes[0].startmasternode ("mn2", True)
    assert_equal (broadcast["status"], "success")
    res = self.nodes[2].broadcaststartmasternode (broadcast["broadcastData"])
    assert_equal (res["status"], "success")
    time.sleep (0.1)
    self.stop_node (0)
    time.sleep (0.1)

    # Check status of the masternodes themselves.
    for i in [1, 2]:
      data = self.nodes[i].getmasternodestatus ()
      assert_equal (data["status"], 4)
      assert_equal (data["txhash"], self.cfg[i - 1].txid)
      assert_equal (data["outputidx"], self.cfg[i - 1].vout)
      assert_equal (data["message"], "Masternode successfully started")

    # Check list of masternodes on node 3.
    lst = self.nodes[3].listmasternodes()
    while len(lst) < 2:
      time.sleep(1)
      lst = self.nodes[3].listmasternodes()

    assert_equal (len (lst), 2)
    assert_equal (lst[0]["tier"], "COPPER")
    assert_equal (lst[1]["tier"], "SILVER")
    for i in range (2):
      assert_equal (lst[i]["status"], "ENABLED")
      assert_equal (lst[i]["addr"],
                    self.nodes[i + 1].getmasternodestatus ()["addr"])
      assert_equal (lst[i]["txhash"], self.cfg[i].txid)
      assert_equal (lst[i]["outidx"], self.cfg[i].vout)

  def sync_masternodes (self):
    print ("Running masternode sync...")

    # Use mocktime ticks to advance the sync status
    # of the node quickly.
    for _ in range (100):
      self.advance_time ()

    for n in [1, 2, 3]:
      status = self.nodes[n].mnsync ("status")
      assert_equal (status["RequestedMasternodeAssets"], 999)

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

    lst = self.nodes[3].listmasternodes ()
    assert_equal (len (lst), 1)
    assert_equal (lst[0]["tier"], "COPPER")
    assert_equal (lst[0]["status"], "ENABLED")
    assert_equal (lst[0]["txhash"], self.cfg[0].txid)

    # Generate more blocks.  Payments to the disabled node will
    # stop at least for the blocks for which we do fresh broadcasts.
    startHeight = self.nodes[3].getblockcount () + 11
    for _ in range (50):
      self.mine_blocks (1)
      self.advance_time (10)
    endHeight = self.nodes[3].getblockcount ()

    addr = self.nodes[1].getmasternodestatus ()["addr"]
    self.verify_number_of_votes_exist_and_tally_winners(startHeight,endHeight,1,addr)

  def check_rewards (self):
    print ("Checking rewards in wallet...")

    self.start_node (0)
    sync_blocks (self.nodes)

    assert_greater_than (self.nodes[0].getbalance ("alloc->mn1"), 0)
    assert_greater_than (self.nodes[0].getbalance ("alloc->mn2"), 0)


if __name__ == '__main__':
  MnStatusTest ().main ()
