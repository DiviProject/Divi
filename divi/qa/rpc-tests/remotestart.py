#!/usr/bin/env python2
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


class MnRemoteStartTest (BitcoinTestFramework):

  def __init__ (self):
    super (MnRemoteStartTest, self).__init__ ()
    self.base_args = ["-debug"]

  def setup_chain (self):
    for i in range (7):
      initialize_datadir (self.options.tmpdir, i)

  def setup_network (self, config_line=None, extra_args=[]):

    # Initially we just start the funding and mining nodes
    # and use them to set up the masternodes.
    self.nodes = [
      start_node (i, self.options.tmpdir, extra_args=self.base_args)
      for i in range(7)
    ]

    # We want to work with mock times that are beyond the genesis
    # block timestamp but before current time (so that nodes being
    # started up and before they get on mocktime aren't rejecting
    # the on-disk blockchain).
    self.time = 1580000000
    assert self.time < time.time ()
    set_node_times (self.nodes, self.time)

    connect_nodes (self.nodes[1], 2)
    connect_nodes (self.nodes[2], 1)
    connect_nodes (self.nodes[1], 4)
    connect_nodes (self.nodes[1], 5)
    connect_nodes (self.nodes[1], 6)
    connect_nodes (self.nodes[2], 4)
    connect_nodes (self.nodes[2], 5)
    connect_nodes (self.nodes[2], 6)
    connect_nodes (self.nodes[3], 4)
    connect_nodes (self.nodes[3], 5)
    connect_nodes (self.nodes[3], 6)
    connect_nodes (self.nodes[4], 5)
    connect_nodes (self.nodes[4], 6)
    connect_nodes (self.nodes[5], 6)
    connect_nodes (self.nodes[0], 3)

    self.is_network_split = False

  def attempt_mnsync(self, ticks=100):
    print ("Attempting masternode sync...")
    # Use mocktime ticks to advance the sync status
    # of the node quickly.
    for _ in range (ticks):
      self.advance_time ()

    result = []
    for n in [1, 2, 3]:
      status = self.nodes[n].mnsync ("status")
      result.append(status["RequestedMasternodeAssets"] == 999)
    return all(result)

  def start_node (self, n, startMN = False):
    """Starts node n (0..2) with the proper arguments
    and masternode config for it."""

    configs = [
      [c.line for c in self.cfg],
      [self.cfg[0].line],
      [self.cfg[1].line],
    ]

    args = self.base_args[:]
    if startMN:
      args.append ("-masternode")
      args.append ("-masternodeprivkey=%s" % self.cfg[n - 1].privkey)
      self.nodes[n] = start_node (n, self.options.tmpdir, extra_args=args, mn_config_lines=configs[n])
    else:
      self.nodes[n] = start_node (n, self.options.tmpdir, extra_args=args)

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
    self.allocate_funds ()
    self.start_masternodes ()

  def allocate_funds (self):
    print ("Allocating masternode funds...")

    # The collateral needs 15 confirmations, and the masternode broadcast
    # signature must be later than that block's timestamp.  Thus we start
    # with a very early timestamp.
    genesis = self.nodes[0].getblockhash (0)
    genesisTime = self.nodes[0].getblockheader (genesis)["time"]
    assert genesisTime < self.time
    set_node_times (self.nodes, self.time)

    self.nodes[0].setgenerate (True, 5)
    sync_blocks (self.nodes)
    self.mine_blocks (25)
    assert_equal (self.nodes[0].getbalance (), 6250)

    # Achive masternode synchronization with peers
    assert self.attempt_mnsync()

    id1 = self.nodes[0].allocatefunds ("masternode", "mn1", "copper")["txhash"]
    id2 = self.nodes[0].allocatefunds ("masternode", "mn2", "silver")["txhash"]

    tx1data = self.nodes[0].gettransaction(id1)["details"][0]["addresses"]
    collateralAddr1 = [ tx1data[i]["address"]  for i in range(len(tx1data)) if tx1data[i]["account"] == "alloc->mn1"  ]
    pubkey1 = self.nodes[0].validateaddress( collateralAddr1[0] )["pubkey"]
    assert(len(collateralAddr1)==1)

    tx2data = self.nodes[0].gettransaction(id2)["details"][0]["addresses"]
    collateralAddr2 = [ tx2data[i]["address"]  for i in range(len(tx2data)) if tx2data[i]["account"] == "alloc->mn2"  ]
    pubkey2 = self.nodes[0].validateaddress( collateralAddr2[0] )["pubkey"]
    assert(len(collateralAddr2)==1)

    addresses = [collateralAddr1[0],collateralAddr2[0]]
    pubkeys = [ str(pubkey1) , str(pubkey2) ]

    sync_mempools (self.nodes)
    self.mine_blocks (1)
    sync_blocks(self.nodes)

    self.funding = [
      fund_masternode (self.nodes[0], "mn1", "copper", id1, "localhost:%d" % p2p_port (1)),
      fund_masternode (self.nodes[0], "mn2", "silver", id2, "localhost:%d" % p2p_port (2)),
    ]
    self.setup = [
      setup_masternode(self.nodes[i+1],self.funding[i],pubkeys[i]) for i in range(2)
    ]
    self.cfg = [
        self.setup[i].cfg for i in range(2)
    ]
    self.sigs = [
        str(self.nodes[0].signmessage(addresses[i] , self.setup[i].message_to_sign , "hex", "hex")) for i in range(2)
    ]

    self.mine_blocks (15*1)
    sync_blocks(self.nodes)

  def start_masternodes (self):
    print ("Starting masternodes...")

    # The masternodes will be inactive until activation.
    time.sleep (0.1)
    assert_equal (self.nodes[3].listmasternodes (), [])

    for i in [1, 2]:
      assert_raises (JSONRPCException, self.nodes[i].getmasternodestatus)

    # Activate the masternodes.  We do not need to keep the
    # cold node online.
    for i in range(1,3):
        copyOfSig = str(self.sigs[i-1])
        copyOfData = str(self.setup[i-1].broadcast_data)
        self.stop_node(i)
        self.start_node (i,True)
        result = self.nodes[0].broadcaststartmasternode(copyOfData, copyOfSig)
        assert_equal(result["status"], "success")
    self.attempt_mnsync()
    for i in [1, 2]:
      data = self.nodes[i].getmasternodestatus ()
      assert_equal (data["status"], 4)
      assert_equal (data["txhash"], self.cfg[i - 1].txid)
      assert_equal (data["outputidx"], self.cfg[i - 1].vout)
      assert_equal (data["message"], "Masternode successfully started")

if __name__ == '__main__':
  MnRemoteStartTest ().main ()
