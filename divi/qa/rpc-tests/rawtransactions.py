#!/usr/bin/env python3
# Copyright (c) 2020 The Divi developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test the raw transactions API.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from util import *


class RawTransactionsTest (BitcoinTestFramework):

    def setup_network (self):
        self.nodes = start_nodes (2, self.options.tmpdir)
        connect_nodes (self.nodes[1], 0)
        self.is_network_split = False
        self.sync_all ()

    def find_output (self, node, value):
        """
        Finds an unspent output owned by the node with the exact
        desired value.  Asserts that one is available.
        """

        for u in node.listunspent ():
            if u["amount"] == value:
                return {"txid": u["txid"], "vout": u["vout"]}

        raise AssertionError ("no output with value %s found" % str (value))

    def extract_outputs (self, decoded):
        """
        From a decoded transaction, extract the outputs into a dict
        with just the addresses / scripts and amounts, which is thus
        easy to compare to golden data.
        """

        res = {}
        for o in decoded["vout"]:
            dest = o["scriptPubKey"]
            if "addresses" in dest:
                assert_equal (len (dest["addresses"]), 1)
                key = dest["addresses"][0]
            else:
                key = dest["hex"]
            res[key] = o["value"]

        return res

    def run_test (self):
        # Give both nodes some outputs valued at exactly 100 DIVI each.
        self.nodes[0].setgenerate (True, 30)
        outputs = {}
        for _ in range (10):
            for n in self.nodes:
                outputs[n.getnewaddress ()] = 100
        self.nodes[0].sendmany ("", outputs)
        self.nodes[0].setgenerate (True, 1)
        sync_blocks (self.nodes)

        # Invalid calls to createrawtransaction.
        txid = "aa" * 32
        addr = self.nodes[1].getnewaddress ()
        invCreates = [
            ({}, {}),
            (["foo"], {}),
            ([{"txid": "invalid", "vout": 0}], {}),
            ([{"txid": "invalid", "vout": 0}], {}),
            ([{"txid": txid}], {}),
            ([{"txid": txid, "vout": -5}], {}),
            ([], "foo"),
            ([], ["foo"]),
            ([], [{addr: 1}, {addr: 2}]),
            ([], [{"aa": 1, "bb": 2}]),
            ([], [{"neither hex nor address": 1}]),
        ]
        for inputs, outputs in invCreates:
            assert_raises (JSONRPCException, self.nodes[0].createrawtransaction,
                           inputs, outputs)

        # The empty transaction.
        empty = self.nodes[0].createrawtransaction ([], {})
        empty2 = self.nodes[0].createrawtransaction ([], [])
        assert_equal (empty2, empty)
        decoded = self.nodes[0].decoderawtransaction (empty)
        assert_equal (decoded["vin"], [])
        assert_equal (decoded["vout"], [])

        # Create a raw transaction with the old-style form.
        addr1 = self.nodes[0].getnewaddress ()
        addr2 = self.nodes[1].getnewaddress ()
        coin = self.find_output (self.nodes[0], 100)
        tx = self.nodes[0].createrawtransaction ([coin], {addr1: 1, addr2: 2})
        decoded = self.nodes[0].decoderawtransaction (tx)
        assert_equal (len (decoded["vin"]), 1)
        assert_equal (decoded["vin"][0]["txid"], coin["txid"])
        assert_equal (decoded["vin"][0]["vout"], coin["vout"])
        assert_equal (self.extract_outputs (decoded), {addr1: 1, addr2: 2})

        # Create a raw transaction based on an array of outputs,
        # also including some raw scripts.
        hex1 = self.nodes[0].validateaddress (addr1)["scriptPubKey"]
        tx = self.nodes[0].createrawtransaction ([], [
            {hex1: 1},
            {"": 0},
            {addr2: 2},
            {"aabbcc": 3},
        ])
        decoded = self.nodes[0].decoderawtransaction (tx)
        assert_equal (self.extract_outputs (decoded), {
            addr1: 1,
            "": 0,
            addr2: 2,
            "aabbcc": 3,
        })

        # Construct a transaction with inputs from both wallets and
        # verify signing (and sending).
        addr1 = self.nodes[0].getnewaddress ("test")
        addr2 = self.nodes[1].getnewaddress ("test")
        coin1 = self.find_output (self.nodes[0], 100)
        coin2 = self.find_output (self.nodes[1], 100)
        tx = self.nodes[0].createrawtransaction ([coin1, coin2],
                                                 {addr1: 50, addr2: 149})
        partial = self.nodes[1].signrawtransaction (tx)
        assert_equal (partial["complete"], False)
        signed = self.nodes[0].signrawtransaction (partial["hex"])
        assert_equal (signed["complete"], True)
        self.nodes[0].sendrawtransaction (signed["hex"])
        self.nodes[0].setgenerate (True, 1)
        sync_blocks (self.nodes)
        assert_equal (self.nodes[0].getbalance ("test"), 50)
        assert_equal (self.nodes[1].getbalance ("test"), 149)


if __name__ == '__main__':
    RawTransactionsTest ().main ()
