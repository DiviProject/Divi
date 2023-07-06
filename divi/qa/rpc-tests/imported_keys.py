#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test -reindex with VerifyBlockIndexTree
#
from test_framework import BitcoinTestFramework
from util import *
import os.path

class EncryptionOfImportedKeys(BitcoinTestFramework):

    def node_initialization(self):
        self.nodes = [None for _ in range(2)]
        self.is_network_split = False
        self.nodes[0] = start_node(0, self.options.tmpdir)
        self.nodes[1] = start_node(1, self.options.tmpdir)
        connect_nodes (self.nodes[0], 1)

    def setup_network(self):
        self.encryption_pass ="medula_oblongata"
        self.node_initialization()

    def reset_chain(self):
        for nodeId in range(len(self.nodes)):
            node = self.nodes[nodeId]
            if node is not None and node.getblockcount() > 2:
                node.invalidateblock(node.getblockhash(1))
                stop_node(node,nodeId)
        wait_bitcoinds()
        for nodeId in range(len(self.nodes)):
            drop_wallet(self.options.tmpdir,nodeId)
            prune_datadir(self.options.tmpdir,nodeId)
        self.node_initialization()



    def restart_importing_node(self, extra_args=None):
        stop_node(self.nodes[1],1)
        self.nodes[1] = start_node(1, self.options.tmpdir,extra_args=extra_args)
        connect_nodes (self.nodes[0], 1)
        sync_blocks(self.nodes)

    def encrypt_and_restart_importing_node(self, extra_args=None,encrypt=True):
        if encrypt:
            self.nodes[1].encryptwallet(self.encryption_pass)
        self.restart_importing_node(extra_args=extra_args)

    def imported_keys_are_forgotten_when_wallet_is_encrypted_prior(self):
        self.nodes[0].setgenerate(30)
        sync_blocks(self.nodes)
        unspentUtxos = self.nodes[0].listunspent()
        spendableUtxos = [ utxo for utxo in unspentUtxos if utxo["confirmations"] > 25 ]
        addressToImport = spendableUtxos[1]["address"]
        exporedKey = self.nodes[0].dumpprivkey(addressToImport)

        self.encrypt_and_restart_importing_node()
        self.nodes[1].walletpassphrase(self.encryption_pass, 0)
        self.nodes[1].importprivkey(exporedKey,"", True)
        assert_equal(self.nodes[1].validateaddress(addressToImport)["ismine"], True)
        self.restart_importing_node()
        assert_equal(self.nodes[1].validateaddress(addressToImport)["ismine"], False)

    def imported_keys_are_usable_when_wallet_is_encrypted_after(self):
        self.nodes[0].setgenerate(30)
        sync_blocks(self.nodes)
        unspentUtxos = self.nodes[0].listunspent()

        spendableUtxos = [ utxo for utxo in unspentUtxos if utxo["confirmations"] > 25 and utxo["amount"] > 1000.0 ]
        addressesToImport = [ utxo["address"] for utxo in spendableUtxos ]
        for addressToImport in addressesToImport:
            exportedKey = self.nodes[0].dumpprivkey(addressToImport)
            self.nodes[1].importprivkey(exportedKey,"", True)
            assert_equal(self.nodes[1].validateaddress(addressToImport)["ismine"], True)
            print("Validating addrIndex: {}".format(addressToImport))

        self.encrypt_and_restart_importing_node(encrypt= True)
        for addressToImport in addressesToImport:
            assert_equal(self.nodes[1].validateaddress(addressToImport)["ismine"], True)
            self.nodes[1].walletpassphrase(self.encryption_pass,0)
            self.nodes[1].sendtoaddress(self.nodes[1].getnewaddress(),1000.0)
            self.restart_importing_node()

    def run_test(self):
        self.imported_keys_are_forgotten_when_wallet_is_encrypted_prior()
        self.reset_chain()
        self.imported_keys_are_usable_when_wallet_is_encrypted_after()

# The tests need better containment cause the first one correctly encrypts, and the second one does not lol

if __name__ == '__main__':
    EncryptionOfImportedKeys().main()


# Encrypting the keys fails to occur (?), because the keys are added by default to the crypto-key-store, which if it isnt crypted
# will add them to the basic key store. If it is crypted it will add them to the crypted keys.
# When encrypting keys, it will attempt to encrypt the basic keys and expect no crypted keys to be present.
# SetCrypted() may be acting in multiple areas to get a sense of the encryption status
# SetCrypted() changes the nature of the wallet locally and temporarily and is left for the wallet to infer.
# As importing changes the apparent state of encryption, the result is that importing keys repeatedly alters the apparent nature of the keys
# So they are sought out in different keypools: basic vs. crypted, hence the failure of an unknown key originating from the keypool