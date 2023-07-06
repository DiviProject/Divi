#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests for rejection of bad blocks

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *
from PowToPosTransition import createPoSStacks, generatePoSBlocks

import codecs
from decimal import Decimal


class BadBlockTests (BitcoinTestFramework):

    def setup_network (self, split=False):
        args = [["-debug=rpc"],["-debug=rpc"]]
        self.nodes = start_nodes (2, self.options.tmpdir, extra_args=args)
        self.node = self.nodes[0]
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False

    def advance_to_PoS(self):
        bad_nodes = [self.node]
        createPoSStacks (bad_nodes, bad_nodes)
        generatePoSBlocks (bad_nodes, 0, 60)
        sync_blocks(self.nodes)

    def build_bad_sig_spend (self):
        utxos = self.node.listunspent ()
        random.shuffle(utxos)
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
        tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )
        tx.vin.append (CTxIn (COutPoint (txid=inp["txid"], n=inp["vout"])))


        unsigned = tx.serialize ().hex ()
        dummyAddress = self.nodes[1].getnewaddress()
        signed = self.nodes[1].signrawtransaction (unsigned,[],[self.nodes[1].dumpprivkey(dummyAddress)])
        assert_raises(JSONRPCException, self.node.sendrawtransaction, signed["hex"])
        decoded = self.node.decoderawtransaction (unsigned)

        return signed["hex"], decoded["txid"]

    def build_double_spends (self):
        utxos = self.node.listunspent ()
        random.shuffle(utxos)
        required = Decimal ('1.00000000')
        inp = None
        for i in range (len (utxos)):
          if utxos[i]["amount"] >= required:
            inp = utxos[i]
            del utxos[i]
            break
        assert inp is not None, "found no suitable output"

        txs = []
        for _ in range(2):
          destAddress = self.node.getnewaddress ()
          data = self.node.validateaddress (destAddress)
          scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
          amountToSend = int ((Decimal (inp["amount"]) - Decimal ('0.1')) * COIN)
          tx = CTransaction ()
          tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )
          tx.vin.append (CTxIn (COutPoint (txid=inp["txid"], n=inp["vout"])))
          unsigned = tx.serialize ().hex ()
          decoded = self.node.decoderawtransaction (unsigned)
          signed = self.node.signrawtransaction (unsigned)
          txs.append(signed["hex"])

        return txs

    def build_overmint_tx (self):
      utxos = self.node.listunspent ()
      random.shuffle(utxos)
      required = Decimal ('1.00000000')
      inp = None
      for i in range (len (utxos)):
        if utxos[i]["amount"] >= required:
          inp = utxos[i]
          del utxos[i]
          break
      assert inp is not None, "found no suitable output"

      destAddress = inp["address"]
      data = self.node.validateaddress (destAddress)
      scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
      undermint = Decimal ('10000.0')
      amountToSend = int ((Decimal (inp["amount"]) - undermint) * COIN)
      amountToReceive = int (undermint * COIN)

      tx = CTransaction ()
      tx.vout.append( CTxOut() )
      tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )
      tx.vout.append( CTxOut(amountToReceive, scriptToSendTo )  )
      tx.vin.append (CTxIn (COutPoint (txid=inp["txid"], n=inp["vout"])))
      unsigned = tx.serialize ().hex ()
      signed = self.node.signrawtransaction (unsigned)
      return signed

    def create_fake_output(self):
        txid = ''.join(random.choice('0123456789abcdef') for n in range(64))
        return {"txid": txid ,"vout":0}

    def build_nonexistent_spend(self):
      utxos = self.node.listunspent ()
      random.shuffle(utxos)
      required = Decimal ('1.00000000')
      inp = None
      for i in range (len (utxos)):
        if utxos[i]["amount"] >= required:
          inp = utxos[i]
          del utxos[i]
          break
      assert inp is not None, "found no suitable output"

      addr = self.node.getnewaddress ("stolen again")
      unsigned = self.node.createrawtransaction ([self.create_fake_output(),{"txid":inp["txid"],"vout":inp["vout"]}], {addr: 0.5})
      signed = self.node.signrawtransaction (unsigned)
      return signed

    def check_unconfirmed (self, txid,node):
        if type (txid) == list:
          for t in txid:
            self.check_unconfirmed (t,node)
          return
        assert_raises(JSONRPCException,node.getrawtransaction,txid)

    def check_unconfirmed (self, txid):
        for node in self.nodes:
          self.check_unconfirmed(txid,node)

    def collect_chainstate_data(self):
        self.chainStates = {}
        for node in self.nodes:
          self.chainStates[node] = [str(node.getblockcount()),str(node.getbestblockhash())]

    def verify_chainstate_unchanged(self):
        for node in self.nodes:
          assert_equal(self.chainStates[node],[str(node.getblockcount()),str(node.getbestblockhash())])

    def run_test (self):
        self.advance_to_PoS()
        self.collect_chainstate_data()

        badTx, _ = self.build_bad_sig_spend ()
        assert_raises(JSONRPCException, self.node.generateblock, {"extratx": [badTx]} )
        self.verify_chainstate_unchanged()

        double_spends = self.build_double_spends ()
        assert_raises(JSONRPCException, self.node.generateblock, {"extratx": double_spends} )
        self.verify_chainstate_unchanged()

        overmint = self.build_overmint_tx()
        assert_raises(JSONRPCException, self.node.generateblock, {"coinstake": overmint["hex"]} )
        self.verify_chainstate_unchanged()

        fake_tx = self.build_nonexistent_spend()
        assert_raises(JSONRPCException, self.node.generateblock, {"extratx": [fake_tx["hex"]]} )
        self.verify_chainstate_unchanged()

        assert_raises(JSONRPCException, self.node.generateblock, {"blockBitsShift": 1} )
        self.verify_chainstate_unchanged()

if __name__ == '__main__':
    BadBlockTests ().main ()
