#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Excercse repeated wallet backups
"""

from test_framework import BitcoinTestFramework
from util import *
from random import randint
import logging

class RepeatedWalletBackupTest(BitcoinTestFramework):

    # This mirrors how the network was setup in the bash test
    def setup_network(self, split=False):
        # nodes 0-2 are spenders, let's give them a keypool=100
        args = [["-keypool=100", "-spendzeroconfchange"]]
        self.nodes = start_nodes(1, self.options.tmpdir, extra_args=args)

    def run_test(self):
        self.nodes[0].setgenerate(25)
        tmpdir = self.options.tmpdir
        self.backup_files_list = []
        for id in range(10):
            filename = tmpdir + "/node0/wallet_"+str(id)+".bak"
            self.backup_files_list.append(filename)
            self.nodes[0].backupwallet(filename)

if __name__ == '__main__':
    RepeatedWalletBackupTest().main()
