#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests handling of pre-signed, stored masternode broadcasts.
#
# This test uses two nodes:  Node 0 is used for mining and funding
# the masternode (as the cold node) and node 1 is the actual masternode
# with empty wallet and imported broadcast (the hot node).

from test_framework import BitcoinTestFramework
from util import *
from masternode import *

import codecs
import time


def modifyHexByte (hexStr, n):
  """Takes a hex string and modifies the n-th byte in it."""

  arr = bytearray (codecs.decode (hexStr, "hex"))
  arr[n] = (arr[n] + 1) % 256

  return arr.hex ()

def txidFromBroadcast (hexStr):
  """Extracts the hex txid from a broadcast in hex."""

  # The prevout txid is the first part of the broadcast data
  # in serialised form.  But we need to reverse the bytes.

  hexRev = hexStr[:64]
  bytesRev = codecs.decode (hexRev, "hex")

  return bytesRev[::-1].hex ()


class MnStoredBroadcastTest (BitcoinTestFramework):

  def setup_network (self, config_line=None):
    args = [["-spendzeroconfchange"]] * 2
    config_lines = [[]] * 2

    if config_line:
      config_lines = [[config_line.line]] * 2
      args[1].append ("-masternode")
      args[1].append ("-masternodeprivkey=%s" % config_line.privkey)

    self.nodes = start_nodes(2, self.options.tmpdir, extra_args=args, mn_config_lines=config_lines)
    connect_nodes_bi (self.nodes, 0, 1)
    self.is_network_split = False
    self.sync_all ()

  def run_test (self):
    print ("Funding masternode...")
    self.nodes[0].setgenerate (True, 30)
    txid = self.nodes[0].allocatefunds ("masternode", "mn", "copper")["txhash"]
    self.nodes[0].setgenerate (True, 1)
    cfg = fund_masternode (self.nodes[0], "mn", "copper", txid, "1.2.3.4")

    print ("Updating masternode.conf...")
    stop_nodes (self.nodes)
    wait_bitcoinds ()
    self.setup_network (config_line=cfg)

    print ("Preparing the masternode broadcast...")
    mnb = self.nodes[0].startmasternode ("mn", True)
    assert_equal (mnb["status"], "success")
    mnb = mnb["broadcastData"]

    # We construct two modified broadcasts:  One for the same prevout with just
    # some of the other data modified, and one for a different prevout.
    # The prevout is the first part in the broadcast data.
    #
    # When we later import all three, the one for the other prevout will stay
    # and the later one for the same prevout (the correct) will replace the
    # modified one.
    mnbOtherPrevout = modifyHexByte (mnb, 3)
    mnbSamePrevout = modifyHexByte (mnb, 100)

    print ("Importing broadcast data...")
    assert_raises (JSONRPCException,
                   self.nodes[1].importmnbroadcast, "invalid")
    assert_equal (self.nodes[1].importmnbroadcast (mnbOtherPrevout), True)
    assert_equal (self.nodes[1].importmnbroadcast (mnb), True)
    assert_equal (self.nodes[1].importmnbroadcast (mnbSamePrevout), True)
    assert_equal (self.nodes[1].importmnbroadcast (mnb), True)
    assert_equal (self.nodes[0].listmnbroadcasts (), [])
    expected = [
      {
        "txhash": txidFromBroadcast (mnbOtherPrevout),
        "outidx": cfg.vout,
        "broadcast": mnbOtherPrevout,
      },
      {
        "txhash": cfg.txid,
        "outidx": cfg.vout,
        "broadcast": mnb,
      }
    ]
    expected.sort (key=lambda x: x["txhash"])
    assert_equal (self.nodes[1].listmnbroadcasts (), expected)

    print ("Restarting node...")
    stop_nodes (self.nodes)
    wait_bitcoinds ()
    self.setup_network (config_line=cfg)

    print ("Testing imported data...")
    assert_equal (self.nodes[1].listmnbroadcasts (), expected)

    # Bump the time, so the stored ping in the broadcast message
    # is expired.  It should be auto-refreshed when starting the masternode.
    blk = self.nodes[0].getblock (self.nodes[0].getbestblockhash ())
    for i in range (100):
      set_node_times (self.nodes, blk["time"] + i * 100)
      time.sleep (0.01)

    print ("Starting masternode with stored broadcast...")
    for n in self.nodes:
      assert_equal (n.listmasternodes (), [])
    res = self.nodes[1].startmasternode ("mn")
    assert_equal (res["status"], "success")
    res = self.nodes[1].getmasternodestatus ()
    assert_equal (res["status"], 4)
    assert_equal (res["message"], "Masternode successfully started")
    time.sleep (1)
    for n in self.nodes:
      lst = n.listmasternodes ()
      assert_equal (len (lst), 1)
      assert_equal (lst[0]["txhash"], cfg.txid)
      assert_equal (lst[0]["status"], "ENABLED")


if __name__ == '__main__':
  MnStoredBroadcastTest ().main ()
