#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the workflow for setting up a masternode vault (with prepared
# unvault tx and destroyed private key), running the masternode with it
# and unvaulting the funds later.
#
# We use seven nodes:
# - node 0 is used to fund and unvault the masternode
# - node 1 is the "hot" masternode
# - node 2 holds the "temporary" vault key and can sign with it
#   (but we use it sparingly)
# - nodes 3-6 are just used to get above the "three full nodes" threshold

from test_framework import BitcoinTestFramework
from util import *
from messages import *
from masternode import *

from binascii import unhexlify
import time


class MnVaultsTest (BitcoinTestFramework):

  def __init__ (self):
    super (MnVaultsTest, self).__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime"]
    self.cfg = None

  def setup_chain (self):
    print ("Initializing test directory " + self.options.tmpdir)
    for i in range (7):
      initialize_datadir (self.options.tmpdir, i)

  def setup_network (self, config_line=None, extra_args=[]):
    self.nodes = [
      start_node (i, self.options.tmpdir, extra_args=self.base_args)
      for i in range (7)
    ]

    # We want to work with mock times that are beyond the genesis
    # block timestamp but before current time (so that nodes being
    # started up and before they get on mocktime aren't rejecting
    # the on-disk blockchain).
    self.time = 1580000000
    assert self.time < time.time ()
    set_node_times (self.nodes, self.time)

    # Nodes 3-5 are connected between each other, and the cluster is
    # also connected to nodes 0-2.
    connect_nodes (self.nodes[3], 4)
    connect_nodes (self.nodes[3], 5)
    connect_nodes (self.nodes[3], 6)
    connect_nodes (self.nodes[4], 5)
    connect_nodes (self.nodes[4], 6)
    connect_nodes (self.nodes[5], 6)
    for i in [0, 1, 2]:
      connect_nodes (self.nodes[i], 3)
      connect_nodes (self.nodes[i], 4)
      connect_nodes (self.nodes[i], 5)
      connect_nodes (self.nodes[i], 6)

    self.is_network_split = False

  def start_node (self, n):
    """Starts node n with the proper arguments
    and masternode config for it."""

    args = self.base_args
    if n == 1 and self.cfg:
      args.append ("-masternode")
      args.append ("-masternodeprivkey=%s" % self.cfg.privkey)

    if self.cfg:
      cfg = [self.cfg.line]
    else:
      cfg = []

    self.nodes[n] = start_node (n, self.options.tmpdir,
                                extra_args=args, mn_config_lines=cfg)
    self.nodes[n].setmocktime (self.time)

    for i in [3, 4, 5, 6]:
      connect_nodes (self.nodes[n], i)

    sync_blocks (self.nodes)

  def stop_node (self, n):
    stop_node (self.nodes[n], n)
    self.nodes[n] = None

  def advance_time (self, dt=1):
    """Advances mocktime by the given number of seconds."""

    self.time += dt
    set_node_times (self.nodes, self.time)

  def mine_blocks (self, n):
    """Mines blocks with node 3."""

    sync_mempools (self.nodes)
    self.nodes[3].setgenerate(True, n)
    sync_blocks (self.nodes)

  def run_test (self):
    self.fund_vault ()
    self.start_masternode ()
    self.get_payments ()
    self.unvault ()

  def fund_vault (self):
    print ("Funding masternode vault...")

    self.nodes[0].setgenerate (True, 5)
    sync_blocks (self.nodes)
    self.mine_blocks (20)

    addr = self.nodes[2].getnewaddress ()
    privkey = self.nodes[2].dumpprivkey (addr)

    amount = 100
    txid = self.nodes[0].sendtoaddress (addr, amount)
    raw = self.nodes[0].getrawtransaction (txid, 1)
    vout = None
    for i in range (len (raw["vout"])):
      o = raw["vout"][i]
      if addr in o["scriptPubKey"]["addresses"]:
        vout = i
        break
    assert vout is not None

    # In a real-world implementation, the unvaulting transaction would
    # be created and backed up before the vaulting one is broadcasted
    # or mined.  But this has no effect on the divid's or the behaviour
    # we want to test here.
    self.mine_blocks (1)

    unvaultAddr = self.nodes[0].getnewaddress ("unvaulted")
    data = self.nodes[0].validateaddress (unvaultAddr)

    tx = CTransaction ()
    tx.vin.append (CTxIn (COutPoint (txid=txid, n=vout)))
    tx.vout.append (CTxOut (amount * COIN, unhexlify (data["scriptPubKey"])))
    unsigned = ToHex (tx)

    validated = self.nodes[0].validateaddress (addr)
    script = validated["scriptPubKey"]
    prevtx = [{"txid": txid, "vout": vout, "scriptPubKey": script}]
    signed = self.nodes[0].signrawtransaction (unsigned, prevtx, [privkey],
                                               "SINGLE|ANYONECANPAY")
    assert_equal (signed["complete"], True)
    self.unvaultTx = signed["hex"]

    self.cfg = fund_masternode (self.nodes[0], "mn", "copper", txid,
                                "localhost:%d" % p2p_port (1))
    # FIXME: Use reward address from node 0.
    self.cfg.rewardAddr = addr

    for i in [0, 1, 2]:
      self.stop_node (i)
      self.start_node (i)

    # Prepare the masternode activation broadcast, without actually
    # relaying it to the network.  After this is done, node 2 with the
    # "temporary" private key is no longer needed at all, and can be
    # shut down for the rest of the test.
    bc = self.nodes[2].startmasternode ("mn", True)
    assert_equal (bc["status"], "success")
    assert_equal (self.nodes[1].importmnbroadcast (bc["broadcastData"]), True)
    self.stop_node (2)

    self.mine_blocks (20)

  def start_masternode (self):
    print ("Starting masternode from vault...")

    # Advance some time to simulate starting the node later (e.g. also when
    # restarting it as necessary during operation).
    for _ in range (100):
      self.advance_time (5)
      time.sleep(0.01)

    # Due to advancing the time without having any masternodes, sync will
    # have failed on the nodes that are up.  Reset the sync now to make
    # sure they will then properly sync together with the other nodes
    # after we start our masternode.
    for n in self.nodes:
      if n is not None:
        n.mnsync ("reset")

    # Now start and activate the masternode based on the stored
    # broadcast message.
    bc = self.nodes[1].startmasternode ("mn")
    assert_equal (bc["status"], "success")

    # Finish masternode sync.
    for _ in range (100):
      self.advance_time ()
      time.sleep(0.01)
    for n in self.nodes:
      if n is not None:
        status = n.mnsync ("status")
        assert_equal (status["currentMasternodeSyncStatus"], 999)

    # Check that the masternode is indeed active.
    data = self.nodes[1].getmasternodestatus ()
    assert_equal (data["status"], 4)
    assert_equal (data["message"], "Masternode successfully started")

  def get_payments (self):
    print ("Receiving masternode payments...")

    # For payments, the masternode needs to be active at least 8000 seconds
    # and we also need at least 100 blocks.  We also need some extra
    # leeway in the time due to the one hour we add to the current time
    # when signing a collateral that is not yet 15 times confirmed.
    self.mine_blocks (100)
    for _ in range (150):
      self.advance_time (100)
      time.sleep(0.01)

    cnt = self.nodes[3].getmasternodecount ()
    assert_equal (cnt["total"], 1)
    assert_equal (cnt["enabled"], 1)
    assert_equal (cnt["inqueue"], 1)

    # Mine some blocks, but advance the time in between and do it
    # one by one so the masternode winners can get broadcast between
    # blocks and such.
    for _ in range (10):
      self.mine_blocks (1)
      self.advance_time (10)
      time.sleep(0.01)

    # Check that some payments were made.
    winners = self.nodes[3].getmasternodewinners ()
    found = False
    for w in winners:
      if w["winner"]["address"] == self.cfg.rewardAddr:
        found = True
        break
    assert_equal (found, True)

    # FIXME: Check in wallet when we have a custom reward address.

  def unvault (self):
    print ("Unvaulting the funds...")

    # The prepared unvaulting tx is just a single input/output pair
    # with no fee attached.  To add the transaction fee, we add another
    # input and output, which is fine due to the SINGLE|ANYONECANPAY signature
    # that we used.

    fee = Decimal ('0.10000000')
    inp = self.nodes[0].listunspent ()[0]
    change = int ((inp["amount"] - fee) * COIN)
    assert_greater_than (change, 0)
    changeAddr = self.nodes[0].getnewaddress ()
    data = self.nodes[0].validateaddress (changeAddr)

    tx = FromHex (CTransaction (), self.unvaultTx)
    tx.vin.append (CTxIn (COutPoint (txid=inp["txid"], n=inp["vout"])))
    tx.vout.append (CTxOut (change, unhexlify (data["scriptPubKey"])))
    partial = ToHex (tx)

    signed = self.nodes[0].signrawtransaction (partial)
    assert_equal (signed["complete"], True)
    self.nodes[0].sendrawtransaction (signed["hex"])
    self.mine_blocks (1)
    assert_equal (self.nodes[0].getbalance ("unvaulted"), 100)


if __name__ == '__main__':
  MnVaultsTest ().main ()
