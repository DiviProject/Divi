#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests coin database stats is functional

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *
from PowToPosTransition import createPoSStacks, generatePoSBlocks

import codecs
from decimal import Decimal
import random

class CoinDBStats (BitcoinTestFramework):

    def setup_network (self, split=False):
        self.config_args = ["-debug","-spentindex=1","-addressindex=1"]
        self.nodes = start_nodes (1, self.options.tmpdir, extra_args=[self.config_args]*2)
        self.is_network_split = False

    def run_test (self):
        node = self.nodes[0]
        node.setgenerate(50)
        data = node.gettxoutsetinfo()
        assert_equal(data["height"],50)
        assert_equal(data["transactions"],50)
        assert_equal(data["txouts"],50)
        assert_equal(data["total_amount"],Decimal(50*1250) )

        blockHeight = 50
        txWithUnspentUTXOCount = 50
        utxoCount = 50
        nonPrunableTxsNow = len( set([x["txid"] for x in node.listunspent()]) )
        for _ in range(8):
            data = node.gettxoutsetinfo()
            sendTo = {}
            for _ in range (0, 8):
                sendTo[node.getnewaddress ()] = 200
                utxoCount+=1
            txid = node.sendmany ("", sendTo)
            node.setgenerate(1)
            data = node.gettxoutsetinfo()
            blockHeight +=1
            nonPrunableTxsBefore = nonPrunableTxsNow
            nonPrunableTxsNow = len( set([x["txid"] for x in node.listunspent()]) )
            txWithUnspentUTXOCount += nonPrunableTxsNow - nonPrunableTxsBefore
            assert_equal(data["height"],blockHeight)
            assert_equal(data["transactions"],txWithUnspentUTXOCount)
            assert_equal(data["txouts"],utxoCount)



if __name__ == '__main__':
    CoinDBStats ().main ()
