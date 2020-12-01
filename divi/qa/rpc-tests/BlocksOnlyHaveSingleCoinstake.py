#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests basic behaviour (standardness, fees) of OP_META transactions.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *
from PowToPosTransition import createPoSStacks, generatePoSBlocks

import codecs
from decimal import Decimal


class BlocksOnlyHaveSingleCoinstake (BitcoinTestFramework):

    def setup_network (self, split=False):
        args = ["-debug"]
        self.nodes = start_nodes (1, self.options.tmpdir, extra_args=[args])
        self.node = self.nodes[0]
        self.is_network_split = False

    def build_coinstake_tx (self):
        utxos = self.node.listunspent ()
        required = Decimal ('1.00000000')
        inp = None
        for i in range (len (utxos)):
          if utxos[i]["amount"] >= required:
            inp = utxos[i]
            del utxos[i]
            break
        assert inp is not None, "found no suitable output"

        destAddress = self.node.getnewaddress ()
        data = self.node.validateaddress (destAddress)
        scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
        amountToSend = int ((Decimal (inp["amount"]) - Decimal ('0.1')) * COIN)
        tx = CTransaction ()
        tx.vout.append( CTxOut(0, CScript() )  )
        tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )
        tx.vin.append (CTxIn (COutPoint (txid=inp["txid"], n=inp["vout"])))


        unsigned = tx.serialize ().hex ()
        signed = self.node.signrawtransaction (unsigned)
        assert_equal (signed["complete"], True)
        assert_raises(JSONRPCException, self.node.sendrawtransaction, signed["hex"])

        decoded = self.node.decoderawtransaction (signed["hex"])

        return signed["hex"], decoded["txid"]

    def check_unconfirmed (self, txid):
        if type (txid) == list:
          for t in txid:
            self.check_unconfirmed (t)
          return
        assert_raises(JSONRPCException,self.node.getrawtransaction,txid)

    def run_test (self):
        createPoSStacks ([self.node], self.nodes)
        generatePoSBlocks (self.nodes, 0, 125)
        sync_blocks(self.nodes)

        secondCoinstake, txid1 = self.build_coinstake_tx ()
        thirdCoinstake, txid2 = self.build_coinstake_tx ()
        assert_raises(JSONRPCException, self.node.generateblock, {"extratx": [secondCoinstake]} )
        self.check_unconfirmed ([txid1, txid2])
        assert_raises(JSONRPCException, self.node.generateblock, {"extratx": [secondCoinstake, thirdCoinstake]} )
        self.check_unconfirmed ([txid1, txid2])

if __name__ == '__main__':
    BlocksOnlyHaveSingleCoinstake ().main ()
