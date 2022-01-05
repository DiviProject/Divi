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

from util import *
from masternode import *

import collections
import time
import sys


class MnAreSafeToRestart (MnTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime"]
    self.time = int(time.time())

  def setup_network (self, config_line=None, extra_args=[]):
    self.nodes = [
      start_node (i, self.options.tmpdir, extra_args=self.base_args)
      for i in range(7)
    ]
    self.setup=[None]*7
    connect_nodes (self.nodes[3], 4)
    connect_nodes (self.nodes[3], 5)
    connect_nodes (self.nodes[3], 6)
    connect_nodes (self.nodes[4], 5)
    connect_nodes (self.nodes[4], 6)
    connect_nodes (self.nodes[5], 6)
    connect_nodes (self.nodes[0], 3)
    connect_nodes (self.nodes[0], 1)
    connect_nodes (self.nodes[0], 2)

  def mine_blocks (self, n,time_window=0.01,blocktime=60):
    """Mines blocks with node 3."""
    for _ in range(n):
      self.time += blocktime
      set_node_times(self.nodes,self.time)
      self.nodes[3].setgenerate( 1)
      sync_blocks (self.nodes)
      time.sleep(time_window)

  def fund_masternodes (self):
    print ("Funding masternodes...")
    self.nodes[0].setgenerate ( 5)
    sync_blocks (self.nodes)
    self.mine_blocks (25)
    assert_equal (self.nodes[0].getbalance (), 6250)
    self.setup_masternode(0,1,"mn1","copper")
    self.setup_masternode(0,2,"mn2","silver")
    self.mine_blocks (16)
    self.stop_masternode_daemons()

  def start_masternodes (self):
    print ("Starting masternodes...")
    self.start_masternode_daemons(updateMockTime=True)
    self.connect_masternodes_to_peers([3, 4, 5, 6],updateMockTime=True)
    sync_blocks(self.nodes)
    # The masternodes will be inactive until activation.
    assert_equal (self.nodes[3].listmasternodes (), [])
    self.check_masternodes_are_locally_inactive()
    # Activate the masternodes.  We do not need to keep the
    # cold node online.
    self.mock_wait(100)
    assert_equal(self.broadcast_start("mn1",True)["status"],"success")
    assert_equal(self.broadcast_start("mn2",True)["status"],"success")

  def shutdown_cold_node(self):
    if self.nodes[0] is not None:
      print("Shutting down cold node...")
      stop_node (self.nodes[0], 0)
      self.nodes[0]=None

  def check_masternode_lists(self):
    # Check list of masternodes on node 3.
    lst = self.wait_for_mn_list_to_sync(self.nodes[3],2)
    print("Checking masternode lists...")
    self.check_list_for_all_masternodes(lst)
    print("Masternodes check out :thumbs_up:")

  def mint_additional_blocks(self):
    print("Minting additional blocks...")
    additional_blocks_to_mint = 25+51
    self.mine_blocks(additional_blocks_to_mint)
    sync_blocks(self.nodes)
    print("Minted "+str(additional_blocks_to_mint)+" additional blocks...")

  def restart_masternodes(self):
    print("Stopping masternodes...")
    self.stop_masternode_daemons()
    print("Masternodes stoped...")
    self.mint_additional_blocks()
    print("Masternodes starting...")
    self.start_masternode_daemons(updateMockTime=True)
    self.connect_masternodes_to_peers([3, 4, 5, 6],updateMockTime=True)
    print("Masternodes started...")


  def run_test (self):
    self.fund_masternodes ()
    self.start_masternodes ()
    self.wait_for_masternodes_to_be_locally_active()
    self.check_masternode_lists()
    self.shutdown_cold_node()
    self.check_masternodes_are_locally_active()
    self.wait_for_mnsync_on_nodes (updateMockTime = True)
    self.restart_masternodes()
    self.wait_for_masternodes_to_be_locally_active()
    self.check_masternode_lists()
    self.check_rewards ()

  def check_rewards (self):
    print ("Checking rewards in wallet...")

    self.nodes[0] = start_node (0, self.options.tmpdir, extra_args=self.base_args[:])
    for i in range(len(self.nodes)):
      if i != 0:
        connect_nodes_bi (self.nodes,0, i)
        self.time += 1
        set_node_times(self.nodes,self.time)
    sync_blocks (self.nodes)

    mn1_balance = self.nodes[0].getbalance ("alloc->mn1")
    mn2_balance = self.nodes[0].getbalance ("alloc->mn2")
    assert_greater_than (mn1_balance, Decimal(100.0)- Decimal(1e-8))
    assert_greater_than (mn2_balance, Decimal(300.0)- Decimal(1e-8))
    assert_greater_than (mn1_balance+mn2_balance, Decimal(400.0 +2.0*540.0) - Decimal(1e-8))
    assert_equal(self.nodes[3].getblockcount(),122)

if __name__ == '__main__':
  MnAreSafeToRestart ().main ()
