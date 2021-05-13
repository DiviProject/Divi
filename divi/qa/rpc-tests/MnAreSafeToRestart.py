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
import sys


class MnAreSafeToRestart (BitcoinTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime", "-nolistenonion"]

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
    self.time = int(time.time())
    connect_nodes (self.nodes[3], 4)
    connect_nodes (self.nodes[3], 5)
    connect_nodes (self.nodes[3], 6)
    connect_nodes (self.nodes[4], 5)
    connect_nodes (self.nodes[4], 6)
    connect_nodes (self.nodes[5], 6)
    connect_nodes (self.nodes[0], 3)

    self.is_network_split = False

  def restart_masternodes(self,delay_time = None):
    print("Stopping masternodes...")
    for nodeId in range(1,3):
      self.stop_node(nodeId)

    print("Masternodes stoped...")
    print("Minting additional blocks...")
    additional_blocks_to_mint = 25
    if delay_time is not None:
      self.mine_blocks(additional_blocks_to_mint,time_window=0.1)

    sync_blocks(self.nodes)
    print("Minted "+str(additional_blocks_to_mint)+" additional blocks...")

    print("Masternodes starting...")
    args = self.base_args[:]
    for n in range(1,3):
      os.remove(self.options.tmpdir + "/node"+str(n)+"/regtest/mncache.dat")
      os.remove(self.options.tmpdir + "/node"+str(n)+"/regtest/mnpayments.dat")
      os.remove(self.options.tmpdir + "/node"+str(n)+"/regtest/peers.dat")
      os.remove(self.options.tmpdir + "/node"+str(n)+"/regtest/netfulfilled.dat")
      shutil.rmtree(self.options.tmpdir + "/node"+str(n)+"/regtest/blocks")
      shutil.rmtree(self.options.tmpdir + "/node"+str(n)+"/regtest/chainstate")
      args.append ("-masternode")
      args.append ("-masternodeprivkey=%s" % self.cfg[n - 1].privkey)
      self.nodes[n] = start_node (n, self.options.tmpdir,
                                extra_args=args, mn_config_lines=self.configs[n])
      for i in [3, 4, 5, 6]:
        connect_nodes (self.nodes[n], i)
    print("Masternodes started...")


  def start_node (self, n):
    """Starts node n (0..2) with the proper arguments
    and masternode config for it."""

    self.configs = [
      [c.line for c in self.cfg],
      [self.cfg[0].line],
      [self.cfg[1].line],
    ]

    args = self.base_args[:]
    if n == 1 or n == 2:
      args.append ("-masternode")
      args.append ("-masternodeprivkey=%s" % self.cfg[n - 1].privkey)

    self.nodes[n] = start_node (n, self.options.tmpdir,
                                extra_args=args, mn_config_lines=self.configs[n])
    for i in [3, 4, 5, 6]:
      connect_nodes (self.nodes[n], i)
    sync_blocks (self.nodes)

  def stop_node (self, n,wait_flag = None):
    """Stops node n (0..2)."""
    stop_node (self.nodes[n], n)
    if wait_flag is not None and wait_flag:
      self.nodes[n].wait()
    self.nodes[n] = None

  def mine_blocks (self, n,time_window=0.01):
    """Mines blocks with node 3."""
    for _ in range(n):
      self.nodes[3].setgenerate(True, 1)
      sync_blocks (self.nodes)
      time.sleep(time_window)

  def run_test (self):
    self.fund_masternodes ()
    self.start_masternodes ()
    self.sync_masternodes (wait=False)
    self.restart_masternodes(delay_time=True)
    self.wait_for_both_masternodes()
    self.check_masternode_statuses()
    self.check_rewards ()

  def fund_masternodes (self):
    print ("Funding masternodes...")

    # The collateral needs 15 confirmations, and the masternode broadcast
    # signature must be later than that block's timestamp.  Thus we start
    # with a very early timestamp.
    genesis = self.nodes[0].getblockhash (0)
    genesisTime = self.nodes[0].getblockheader (genesis)["time"]
    assert genesisTime < self.time

    self.nodes[0].setgenerate (True, 5)
    sync_blocks (self.nodes)
    self.mine_blocks (25)
    assert_equal (self.nodes[0].getbalance (), 6250)

    id1 = self.nodes[0].allocatefunds ("masternode", "mn1", "copper")["txhash"]
    id2 = self.nodes[0].allocatefunds ("masternode", "mn2", "silver")["txhash"]
    sync_mempools (self.nodes)
    self.mine_blocks (16)

    self.cfg = [
      fund_masternode (self.nodes[0], "mn1", "copper", id1, "localhost:%d" % p2p_port (1)),
      fund_masternode (self.nodes[0], "mn2", "silver", id2, "localhost:%d" % p2p_port (2)),
    ]

  def check_masternode_statuses(self):
    print("Checking masternode statuses...")
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
    if lst[0]["tier"] != "COPPER":
      lst[1],lst[0] = lst[0],lst[1]
    assert_equal (lst[0]["tier"], "COPPER")
    assert_equal (lst[1]["tier"], "SILVER")
    for i in range (2):
      assert_equal (lst[i]["status"], "ENABLED")
      assert_equal (lst[i]["addr"],
                    self.nodes[i + 1].getmasternodestatus ()["addr"])
      assert_equal (lst[i]["txhash"], self.cfg[i].txid)
      assert_equal (lst[i]["outidx"], self.cfg[i].vout)
    print("Masternodes check out :thumbs_up:")

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
    self.nodes[0].startmasternode ("mn2")
    time.sleep(0.1)
    self.stop_node (0)
    time.sleep (0.1)

    # Check status of the masternodes themselves.
    self.check_masternode_statuses()

  def sync_masternodes (self,wait=True):
    print ("Running masternode sync...")

    self.other_time = int(time.time())
    for n in [1, 2, 3]:
      while True:
        status = self.nodes[n].mnsync ("status")
        if str(status["RequestedMasternodeAssets"])!=str(999):
          print("...")
          if wait:
            time.sleep(10)
          else:
            for _ in range(100):
              self.other_time += 1
              set_node_times(self.nodes,self.other_time)
              time.sleep(.01)
        else:
          break

  def wait_for_both_masternodes(self,maximumTries=3):
    count_of_nodes_working = 0
    while maximumTries>0:
      all_nodes_found = True
      time.sleep(.5)
      count_of_nodes_working = 0
      for n in [1, 2]:
        try:
          data = self.nodes[n].getmasternodestatus ()
          should_pass = data["status"]==4
          if should_pass:
            count_of_nodes_working += 1
          else:
            all_nodes_found = False
        except:
          all_nodes_found = False
          maximumTries -= 1
          for _ in range(100):
            self.other_time += 1
            set_node_times(self.nodes,self.other_time)
            time.sleep(.01)
      print("Waiting for masternodes to accept their own ping...{}/2".format(count_of_nodes_working))
      if all_nodes_found:
        break
    assert_equal(count_of_nodes_working,2)


  def check_rewards (self):
    print ("Checking rewards in wallet...")

    self.start_node (0)
    sync_blocks (self.nodes)

    assert_greater_than (self.nodes[0].getbalance ("alloc->mn1"), 0)
    assert_greater_than (self.nodes[0].getbalance ("alloc->mn2"), 0)


if __name__ == '__main__':
  MnAreSafeToRestart ().main ()
