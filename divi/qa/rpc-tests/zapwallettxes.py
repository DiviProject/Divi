#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests -zapwallettxes=<mode>

from test_framework import BitcoinTestFramework
from util import *


def check_balance(n, expected, account="*"):
  # Take fees into account, and only check for approximate equality.
  actual = n.getbalance(account, 0)
  assert actual > expected - 1, str((actual, expected))
  assert actual < expected + 1, str((actual, expected))


class ZapWalletTxesTest (BitcoinTestFramework):

    def setup_network(self, extra=[[]]*2):
        # We use two nodes that will in the end use the different
        # zapwallettxes modes, but otherwise are not related (and just
        # mimic each other).  Thus we do not set up a network connection
        # between the nodes.
        args = [["-spendzeroconfchange"]] * 2
        combined_args = [a + b for a, b in zip(extra, args)]
        self.nodes = start_nodes(2, self.options.tmpdir, extra_args=combined_args)
        self.is_network_split=True

    def run_test (self):
        addr = "yKqewKnfiTXZyjpXeTpTBrFVJT7w7d1f7G"

        print ("Generating some blocks and transactions...")
        confirmed = []
        unconfirmed = []
        for n in self.nodes:
            # 21 blocks, 1 mature: 1250 DIVI
            n.setgenerate(True, 21)
            check_balance(n, 1250)

            # Send 10 DIVI
            txids = []
            txids.append(n.sendtoaddress(addr, 10))
            check_balance(n, 1240)

            # Move 10 DIVI to testaccount
            n.move("", "testaccount", 10)
            check_balance(n, 10, "testaccount")

            # Send 1 DIVI from testaccount
            txids.append(n.sendfrom("testaccount", addr, 1))
            check_balance(n, 9, "testaccount")
            check_balance(n, 1239)

            # Confirm transactions
            n.setgenerate(True, 1)

            # Create unconfirmed transaction
            txid = n.sendtoaddress(addr, 1)
            unconfirmed.append(txid)

            # We created another 1250 and spent 1 in the meantime
            check_balance(n, 2488)

            # The unconfirmed transaction should be there
            n.gettransaction(txid)

            confirmed.append(txids)

        # Stop the nodes
        print ("Restarting the nodes with -zapwallettxes...")
        stop_nodes(self.nodes)
        wait_bitcoinds()

        # Restart with -zapwallettxes
        self.setup_network(extra=[["-zapwallettxes=1"], ["-zapwallettxes=2"]])

        # The confirmed transactions should still be there, but the
        # unconfirmed one should be gone.
        print ("Verifying after-zap wallets...")
        for n, c, u in zip(self.nodes, confirmed, unconfirmed):
            for txid in c:
                n.gettransaction(txid)
            assert_raises(JSONRPCException, n.gettransaction, u)

        # Mode 1 should keep the metadata
        check_balance(self.nodes[0], 9, "testaccount")
        # Mode 2 should clear it
        check_balance(self.nodes[1], 10, "testaccount")


if __name__ == '__main__':
    ZapWalletTxesTest ().main ()
