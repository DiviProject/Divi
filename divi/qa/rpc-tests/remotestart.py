#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests starting of masternodes remotely

from test_framework import BitcoinTestFramework
from util import *
from masternode import *

import collections
import time


class MnRemoteStartTest (MnTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime"]
    self.number_of_nodes = 7

  def add_options(self, parser):
          parser.add_option("--outdated_ping", dest="outdated_ping", default=False, action="store_true",
                            help="Test outdated ping recovery")

  def connect_all_nodes(self):
    for i in range(self.number_of_nodes):
      for j in range(self.number_of_nodes):
        if i < j:
          connect_nodes_bi(self.nodes,i,j)

  def setup_network (self, config_line=None, extra_args=[]):
    self.nodes = [
      start_node (i, self.options.tmpdir, extra_args=self.base_args)
      for i in range(self.number_of_nodes)
    ]
    self.setup=[None]*self.number_of_nodes
    self.time = 1580000000
    set_node_times (self.nodes, self.time)
    self.connect_all_nodes()
    self.is_network_split = False

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

    self.nodes[3].setgenerate( n)
    sync_blocks (self.nodes)

  def run_test (self):
    self.allocate_funds ()
    self.start_masternodes ()

  def allocate_funds (self):
    print ("Allocating masternode funds...")
    set_node_times (self.nodes, self.time)

    self.nodes[0].setgenerate ( 5)
    sync_blocks (self.nodes)
    self.mine_blocks (25)
    assert_equal (self.nodes[0].getbalance (), 6250)

    controlNode = self.nodes[0]
    self.setup_masternode(0,1,"mn1","copper")
    self.setup_masternode(0,2,"mn2","silver")
    self.cfg = [
        x.cfg for x in self.setup if x
    ]
    self.sigs = [
        str(controlNode.signmessage(x.address, x.message_to_sign , "hex", "hex")) for x in [self.setup[1],self.setup[2]]
    ]

    self.mine_blocks (15*1)
    sync_blocks(self.nodes)

  def start_masternodes (self):
    print ("Starting masternodes...")

    # The masternodes will be inactive until activation.
    time.sleep (0.1)
    assert_equal (self.nodes[3].listmasternodes (), [])
    self.check_masternodes_are_locally_inactive()

    # Activate the masternodes.  We do not need to keep the
    # cold node online.
    if self.options.outdated_ping:
      # Broadcasting nodes perceive the ping as being out of date but not
      # the broadcast itself
      self.time += 2*60*60
      set_node_times(self.nodes[1:3],self.time)
      self.connect_all_nodes()

    self.stop_masternode_daemons()
    self.start_masternode_daemons()
    self.connect_masternodes_to_peers([3,4,5,6],updateMockTime=True)
    for i in range(2):
        if self.options.outdated_ping:
          result = self.broadcast_start(self.cfg[i].alias,False,self.sigs[i])
        else:
          result = self.broadcast_start(self.cfg[i].alias,True,self.sigs[i])
        assert_equal(result["status"], "success")
    self.wait_for_masternodes_to_be_locally_active(updateMockTime=True)

if __name__ == '__main__':
  MnRemoteStartTest ().main ()
