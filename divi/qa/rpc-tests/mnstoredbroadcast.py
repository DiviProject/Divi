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
from authproxy import JSONRPCException

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


class MnStoredBroadcastTest (MnTestFramework):

  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime", "-spendzeroconfchange"]
    self.number_of_nodes = 2

  def setup_network (self, config_line=None):
    self.nodes = start_nodes(self.number_of_nodes, self.options.tmpdir, extra_args=[self.base_args]*2)
    self.setup = [None]*self.number_of_nodes
    connect_nodes(self.nodes[0],1)
    self.is_network_split = False
    self.sync_all ()

  def run_test (self):
    print ("Funding masternode...")
    self.nodes[0].setgenerate(30)
    self.setup_masternode(0,1,"mn","copper")
    self.nodes[0].setgenerate ( 16)

    print ("Updating masternode.conf...")
    self.stop_masternode_daemons()
    self.start_masternode_daemons()
    connect_nodes(self.nodes[0],1)

    print ("Preparing the masternode broadcast...")
    cfg = self.setup[1].cfg
    mnb = self.nodes[0].signmnbroadcast(self.setup[1].broadcast_data)["broadcast_data"]

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
    self.stop_masternode_daemons()
    self.start_masternode_daemons()
    connect_nodes(self.nodes[0],1)

    print ("Testing imported data...")
    assert_equal (self.nodes[1].listmnbroadcasts (), expected)

    # Bump the time, so the stored ping in the broadcast message
    # is expired.  It should be auto-refreshed when starting the masternode.
    blk = self.nodes[0].getblock (self.nodes[0].getbestblockhash ())
    for i in range (100):
      self.time = blk["time"] + i * 100
      set_node_times (self.nodes, self.time)
      time.sleep (0.01)
    connect_nodes(self.nodes[0],1)

    print ("Starting masternode with stored broadcast...")
    for n in self.nodes:
      assert_equal (n.listmasternodes (), [])

    res = self.nodes[1].startmasternode ("mn")
    assert_equal (res["status"], "success")
    res = self.nodes[1].getmasternodestatus ()
    assert_equal (res["status"], 4)
    assert_equal (res["message"], "Masternode successfully started")
    time.sleep (1)
    self.wait_for_mn_list_to_sync(self.nodes[0],1);
    for n in self.nodes:
      lst = n.listmasternodes ()
      assert_equal (len (lst), 1)
      assert_equal (lst[0]["txhash"], cfg.txid)
      assert_equal (lst[0]["status"], "ENABLED")


if __name__ == '__main__':
  MnStoredBroadcastTest ().main ()
