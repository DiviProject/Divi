#!/usr/bin/env python2
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests wallet handling of masternode collateral.  In particular, we
# allocate funds, assign it to a masternode (but without running the
# masternode) and verify the expected locking of the collateral UTXO.

from test_framework import BitcoinTestFramework
from util import *
from masternode import *


class MnCollateralTest (BitcoinTestFramework):

  def setup_network (self, config_line=None, extra_args=[]):
    args = [(extra_args + ["-spendzeroconfchange"])] * 2
    config_lines = [[]] * 2
    if config_line is not None:
      config_lines[0] = [config_line]
    self.nodes = start_nodes(2, self.options.tmpdir, extra_args=args, mn_config_lines=config_lines)
    connect_nodes_bi (self.nodes, 0, 1)
    self.is_network_split = False
    self.sync_all ()

  def mine_blocks (self, n):
    """Mines blocks with node 1.  We use node 0 for the test, so that
    this ensures that node does not get unexpected coins."""

    sync_mempools (self.nodes)
    self.nodes[1].setgenerate(True, n)
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
    node.setgenerate (True, 1)
    sync_blocks (self.nodes)
    self.mine_blocks (20)
    self.check_balance (1250)

    # Allocate some funds and use them to construct a config line.
    txid = node.allocatefunds ("masternode", "spent", "silver")["txhash"]
    self.mine_blocks (1)
    self.check_balance (300, "alloc->spent")
    self.check_balance (1250)
    cfg = fund_masternode (node, "spent", "silver", txid, "1.2.3.4")
    assert_equal (cfg.alias, "spent")
    assert_equal (cfg.ip, "1.2.3.4:51476")
    assert_equal (cfg.txid, txid)
    assert_equal (node.gettxout (cfg.txid, cfg.vout)["value"], 300)

    # It should still be possible to spend the coins, invalidating the
    # masternode funding.
    node.sendtoaddress (node.getnewaddress (), 1000)
    self.mine_blocks (1)
    self.check_balance (1250)
    assert_equal (node.gettxout (cfg.txid, cfg.vout), None)

    # Allocate some more funds without spending them.
    txid = node.allocatefunds ("masternode", "gold", "gold")["txhash"]
    self.mine_blocks (1)
    self.check_balance (1000, "alloc->gold")
    self.check_balance (1250)
    cfg = fund_masternode (node, "spent", "gold", txid, "1.2.3.4:1024")
    assert_equal (cfg.ip, "1.2.3.4:1024")
    assert_equal (node.gettxout (cfg.txid, cfg.vout)["value"], 1000)

    # Restart with the config line added.
    stop_nodes (self.nodes)
    wait_bitcoinds ()
    self.setup_network (config_line=cfg.line)
    node = self.nodes[0]

    # Now spending the locked coins will not work (but spending less is fine).
    assert_equal (node.listlockunspent (), [
      {"txid": cfg.txid, "vout": cfg.vout},
    ])
    assert_raises(JSONRPCException, node.sendtoaddress, node.getnewaddress (), 500)
    node.sendtoaddress (node.getnewaddress (), 200)
    self.mine_blocks (1)
    self.check_balance (1250)

    # Restart without locking.
    stop_nodes (self.nodes)
    wait_bitcoinds ()
    self.setup_network (extra_args=["-mnconflock=0"], config_line=cfg.line)
    node = self.nodes[0]

    # Now we can spend the collateral.
    assert_equal (node.listlockunspent (), [])
    node.sendtoaddress (node.getnewaddress (), 1200)
    self.mine_blocks (1)
    self.check_balance (1250)


if __name__ == '__main__':
  MnCollateralTest ().main ()
