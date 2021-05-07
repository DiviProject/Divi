#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests how the network behaves (including masternodes) if there
# are no blocks for a long time and the nodes consider themselves
# no longer blockchain synced (IsBlockchainSynced).  Even in that
# case, the network should be able to generate new blocks and
# get started again.
#
# We use eight nodes:
# - node 0 will be restarted and then not mn sync until the blockchain
#   advances further
# - node 1 will remain online and stay mn synced (but will consider
#   itself not blockchain synced)
# - nodes 2-7 are masternodes; they bring us above the "three full nodes"
#   threshold, and also make sure that we enforce reward payments (which
#   needs six votes)

from test_framework import BitcoinTestFramework
from util import *
from masternode import *

from PowToPosTransition import createPoSStacks

import collections
import os
import os.path
import time


class NoBlocksForLongTimeTest (BitcoinTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime", "-nolistenonion"]

  def setup_chain (self):
    print ("Initializing test directory " + self.options.tmpdir)
    for i in range (8):
      initialize_datadir (self.options.tmpdir, i)

  def setup_network (self, config_line=None, extra_args=[]):
    self.nodes = [
      start_node (i, self.options.tmpdir, extra_args=self.base_args)
      for i in range (8)
    ]

    # We want to work with mock times that are beyond the genesis
    # block timestamp but before current time (so that nodes being
    # started up and before they get on mocktime aren't rejecting
    # the on-disk blockchain).
    self.time = 1_580_000_000
    assert self.time < time.time ()
    set_node_times (self.nodes, self.time)

    for n in range (8):
      for m in range (n + 1, 8):
        connect_nodes (self.nodes[n], m)

    self.is_network_split = False

  def start_node (self, n):
    """Starts node n with the proper arguments
    and masternode config for it."""

    configs = []
    args = self.base_args[:]
    if n in self.cfg:
      configs.append (self.cfg[n].line)
      args.append ("-masternode")
      args.append ("-masternodeprivkey=%s" % self.cfg[n].privkey)

    self.nodes[n] = start_node (n, self.options.tmpdir,
                                extra_args=args, mn_config_lines=configs)
    self.nodes[n].setmocktime (self.time)

    for m in range (8):
      if self.nodes[m] and n != m:
        connect_nodes (self.nodes[n], m)
    sync_blocks (self.nodes)

  def stop_node (self, n):
    """Stops node n."""

    if self.nodes[n]:
      stop_node (self.nodes[n], n)
      self.nodes[n] = None

  def advance_time (self, dt=1):
    """Advances mocktime by the given number of seconds."""

    self.time += dt
    set_node_times (self.nodes, self.time)

  def fund_masternodes (self):
    print ("Funding masternodes...")

    self.nodes[1].setgenerate (True, 30)
    assert_greater_than (self.nodes[1].getbalance (), 1200)

    sendTo = {}
    for n in range (2, 8):
      sendTo[self.nodes[n].getnewaddress ()] = 200
    self.nodes[1].sendmany ("", sendTo)
    self.nodes[1].setgenerate (True, 1)
    sync_blocks (self.nodes)

    self.cfg = {}
    for n in range (2, 8):
      data = self.nodes[n].allocatefunds ("masternode", "mn", "copper")
      txid = data["txhash"]
      self.cfg[n] = fund_masternode (self.nodes[n], "mn", "copper", txid,
                                     "localhost:%d" % p2p_port (n))

    sync_mempools (self.nodes)
    self.nodes[1].setgenerate (True, 15)
    set_node_times (self.nodes, self.time)
    self.nodes[1].setgenerate (True, 1)
    sync_blocks (self.nodes)

  def start_masternodes (self):
    print ("Starting masternodes...")

    for n in self.cfg.keys ():
      self.stop_node (n)
      self.start_node (n)

    # Make sure all nodes are properly connected before we go on
    # starting the masternodes (we don't want any to miss some broadcasts).
    allConnected = False
    while not allConnected:
      time.sleep (0.1)
      allConnected = True
      for n in self.nodes:
        if len (n.getpeerinfo ()) < len (self.nodes) - 1:
          allConnected = False
    sync_blocks (self.nodes)

    for n in self.cfg.keys ():
      res = self.nodes[n].startmasternode ("mn")
      assert_equal (res["status"], "success")

      # Check status of the masternode by itself.
      data = self.nodes[n].getmasternodestatus ()
      assert_equal (data["status"], 4)
      assert_equal (data["txhash"], self.cfg[n].txid)
      assert_equal (data["outputidx"], self.cfg[n].vout)
      assert_equal (data["message"], "Masternode successfully started")

    # Check list of masternodes on node 0.
    lst = []
    while len (lst) < 6:
      time.sleep (1)
      lst = self.nodes[0].listmasternodes ()

    assert_equal (len (lst), 6)
    for l in lst:
      assert_equal (l["tier"], "COPPER")
      assert_equal (l["status"], "ENABLED")

  def sync_masternodes (self):
    # Use mocktime ticks to advance the sync status
    # of the node quickly.
    for _ in range (100):
      self.advance_time ()

    for n in self.nodes:
      status = n.mnsync ("status")
      assert_equal (status["RequestedMasternodeAssets"], 999)

  def time_out_blockchain_sync (self):
    """
    Lets the blockchain sync "time out", i.e. waits for more than ten minutes
    with mocktime.  After that duration without new blocks, IsBlockchainSynced
    returns false; however, for nodes that are still running, the value is
    cached and thus remains true.  We restart node 0, which will then consider
    itself not blockchain synced and also won't run a full mn sync.
    """

    self.stop_node (0)
    for _ in range (10):
      self.advance_time (100)
    self.start_node (0)
    for _ in range (10):
      self.advance_time ()

    status = self.nodes[0].mnsync ("status")
    assert_equal (status["IsBlockchainSynced"], False)
    assert_equal (status["RequestedMasternodeAssets"], 1)

    status = self.nodes[1].mnsync ("status")
    assert_equal (status["IsBlockchainSynced"], True)
    assert_equal (status["RequestedMasternodeAssets"], 999)

  def run_test (self):
    createPoSStacks ([self.nodes[0], self.nodes[1]], self.nodes)

    self.fund_masternodes ()
    self.start_masternodes ()

    # Run initial masternode sync.  Also, for payments to be active, we
    # need at least 100 blocks and the masternodes need to be active
    # for 8'000 seconds.
    print ("Running initial masternode sync...")
    self.sync_masternodes ()
    for _ in range (100):
      self.nodes[1].setgenerate (True, 1)
      self.advance_time (100)
    for n in self.nodes:
      assert_equal (n.getmasternodecount ()["inqueue"], 6)

    # Lottery blocks are every 10th on regtest, and treasury blocks the
    # ones after them.  We want to test masternode rewards mainly, so
    # make sure the next couple of blocks are neither type.  We get the
    # current block height, and then generate more blocks so that we
    # end up with a height that is 1 mod 10; that then means the next
    # block is mod 2 and so the next couple of blocks are neither type
    # of special payment.
    cnt = self.nodes[1].getblockcount ()
    self.nodes[1].setgenerate (True, 11 - (cnt % 10))
    assert_equal (self.nodes[1].getblockcount () % 10, 1)
    sync_blocks (self.nodes)

    # After ten minutes, the blockchain is considered no longer synced
    # if the node is restarted (but if not, then the value is cached
    # and remains true during the process' life).
    #
    # Make sure that it is fine to generate blocks even after then
    # with a node that is still masternode synced.
    print ("Generating block with mn synced node...")
    self.time_out_blockchain_sync ()
    self.nodes[1].setgenerate (True, 1)
    sync_blocks (self.nodes)
    self.sync_masternodes ()

    # Similarly, it should also work to generate blocks with a restarted
    # node that does not consider itself mn synced.  It still has the
    # cached list of masternodes and will thus set the right payment.
    print ("Generating block with not mn synced node...")
    self.time_out_blockchain_sync ()
    assert_greater_than (len (self.nodes[0].listmasternodes ()), 0)
    self.nodes[0].setgenerate (True, 1)
    sync_blocks (self.nodes)
    self.sync_masternodes ()

    # If we restart the node and remove the masternode cache, it won't
    # have a list of masternodes (as it will also not process any messages)
    # and will thus not produce a valid block.
    print ("Generating block with node without masternode list...")
    self.stop_node (0)
    for f in ["mncache.dat", "mnpayments.dat"]:
      os.remove (os.path.join (self.options.tmpdir, "node0", "regtest", f))
    self.time_out_blockchain_sync ()
    assert_equal (self.nodes[0].listmasternodes (), [])
    self.nodes[0].setgenerate (True, 1)
    time.sleep (1)
    assert_equal (self.nodes[0].getblockcount (),
                  self.nodes[1].getblockcount () + 1)


if __name__ == '__main__':
  NoBlocksForLongTimeTest ().main ()
