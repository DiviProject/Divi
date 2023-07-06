#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests wallet handling of masternode collateral.  In particular, we
# allocate funds, assign it to a masternode (but without running the
# masternode) and verify the expected locking of the collateral UTXO.

from util import *
from masternode import *
from authproxy import JSONRPCException


class MnCollateralTest (MnTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime"]
    self.time = int(time.time())
    self.number_of_nodes = 2

  def setup_network (self, config_line=None, extra_args=[]):
    args = [(extra_args + ["-spendzeroconfchange"])] * 2
    config_lines = [[]] * 2
    if config_line is not None:
      config_lines[0] = [config_line]
    self.nodes = start_nodes(2, self.options.tmpdir, extra_args=args, mn_config_lines=config_lines)
    self.setup = [None]*self.number_of_nodes
    connect_nodes_bi (self.nodes, 0, 1)
    self.is_network_split = False
    self.sync_all ()

  def mine_blocks (self, n):
    """Mines blocks with node 1.  We use node 0 for the test, so that
    this ensures that node does not get unexpected coins."""
    sync_mempools (self.nodes)
    self.nodes[1].setgenerate( n)
    sync_blocks (self.nodes)

  def check_balance (self, expected, account="*"):
    """Verifies that the balance of node 0 matches the expected amount,
    up to some epsilon for fees."""
    actual = self.nodes[0].getbalance (account)
    eps = 1
    assert_greater_than (actual, expected - eps)
    assert_greater_than (expected + eps, actual)

  def run_test (self):
    # Give 1250 matured coins to node 0.  All future blocks will be mined
    # with node 1, so that we can verify the expected balance.
    node = self.nodes[0]
    node.setgenerate ( 1)
    sync_blocks (self.nodes)
    self.mine_blocks (20)
    self.check_balance (1250)

    # Allocate some funds and use them to construct a config line.
    self.setup_masternode(0,1,"spent","silver")
    self.mine_blocks (1)
    self.check_balance (300, "alloc->spent")
    self.check_balance (1250)
    cfg = self.setup[1].cfg
    assert_equal (node.gettxout (cfg.txid, cfg.vout)["value"], 300)

    # It should still be possible to spend the coins, invalidating the
    # masternode funding.
    node.sendfrom ("alloc->spent",node.getnewaddress (), 299.999)
    self.mine_blocks (1)
    self.check_balance (1250)
    assert_equal (node.gettxout (cfg.txid, cfg.vout), None)

    # Allocate some more funds without spending them.
    self.setup_masternode(0,1,"gold","gold")
    self.mine_blocks (1)
    self.check_balance (1000, "alloc->gold")
    self.check_balance (1250)
    cfg = self.setup[1].cfg
    assert_equal (node.gettxout (cfg.txid, cfg.vout)["value"], 1000)

    # Restart with the config line added.
    self.stop_masternode_daemons()
    self.start_masternode_daemons()
    stop_node(self.nodes[0],0)
    self.nodes[0] = start_node(0,dirname=self.options.tmpdir,mn_config_lines=[cfg.line])
    node = self.nodes[0]
    connect_nodes_bi (self.nodes, 0, 1)

    # Now spending the locked coins will not work (but spending less is fine).
    assert_equal (node.listlockunspent (), [
      {"txid": cfg.txid, "vout": cfg.vout},
    ])
    assert_raises(JSONRPCException, node.sendtoaddress, node.getnewaddress (), 500)
    node.sendtoaddress (node.getnewaddress (), 200)
    self.mine_blocks (1)
    self.check_balance (1250)

    # Restart without locking.
    stop_node(self.nodes[0],0)
    self.nodes[0] = start_node(0,dirname=self.options.tmpdir,extra_args=["-mnconflock=0"],mn_config_lines=[cfg.line])
    node = self.nodes[0]
    connect_nodes_bi (self.nodes, 0, 1)

    # Now we can spend the collateral.
    assert_equal (node.listlockunspent (), [])
    node.sendfrom ("alloc->gold", node.getnewaddress (), 999.999)
    self.mine_blocks (1)
    self.check_balance (1250)


if __name__ == '__main__':
  MnCollateralTest ().main ()
