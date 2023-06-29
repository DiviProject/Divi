#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Exercise the wallet backup code.  Ported from walletbackup.sh.

Test case is:
4 nodes. 1 2 and 3 send transactions between each other,
fourth node is a miner.
1 2 3 each mine a block to start, then
Miner creates 100 blocks so 1 2 3 each have 50 mature
coins to spend.
Then 5 iterations of 1/2/3 sending coins amongst
themselves to get transactions in the wallets,
and the miner mining one block.

Wallets are backed up using backupwallet.
Then 5 more iterations of transactions and mining a block.

Miner then generates 101 more blocks, so any
transaction fees paid mature.

Sanity check:
  Sum(1,2,3,4 balances) == 114*50

1/2/3 are shutdown, and their wallets erased.
Then restore using wallet.dat backup. And
confirm 1/2/3/4 balances are same as before.
"""

from test_framework import BitcoinTestFramework
from util import *
from random import randint
import logging
import sys
logging.basicConfig(format='%(levelname)s:%(message)s', level=logging.INFO, stream=sys.stdout)

class WalletBackupTest(BitcoinTestFramework):

    # This mirrors how the network was setup in the bash test
    def setup_network(self, split=False):
        # nodes 0-2 are spenders, let's give them a keypool=100
        extra_args = [["-keypool=100", "-spendzeroconfchange"]] * 3 + [[]]
        self.nodes = start_nodes(4, self.options.tmpdir, extra_args)
        self.time = int(time.time())
        set_node_times(self.nodes,self.time)
        connect_nodes(self.nodes[0], 3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        connect_nodes(self.nodes[2], 0)
        self.is_network_split=False
        self.sync_all()

    def one_send(self, from_node, to_address):
        if (randint(1,2) == 1):
            amount = Decimal(randint(1,10)) / Decimal(10)
            txid = self.nodes[from_node].sendtoaddress(to_address, amount)
            fee = Decimal(self.nodes[from_node].gettransaction(txid)["fee"])
            assert fee > 0
            self.fees += Decimal(self.nodes[from_node].gettransaction(txid)["fee"])

    def do_one_round(self):
        a0 = self.nodes[0].getnewaddress()
        a1 = self.nodes[1].getnewaddress()
        a2 = self.nodes[2].getnewaddress()

        self.one_send(0, a1)
        self.one_send(0, a2)
        self.one_send(1, a0)
        self.one_send(1, a2)
        self.one_send(2, a0)
        self.one_send(2, a1)

        # Have the miner (node3) mine a block.
        # Must sync mempools before mining.
        sync_mempools(self.nodes)
        self.nodes[3].setgenerate( 1)
        assert_equal(self.nodes[3].getrawmempool(), [])
        sync_blocks(self.nodes)

    def start_three(self):
        self.nodes[0] = start_node(0, self.options.tmpdir)
        self.nodes[1] = start_node(1, self.options.tmpdir)
        self.nodes[2] = start_node(2, self.options.tmpdir)
        connect_nodes(self.nodes[0], 3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        connect_nodes(self.nodes[2], 0)

    def stop_three(self):
        stop_node(self.nodes[0], 0)
        stop_node(self.nodes[1], 1)
        stop_node(self.nodes[2], 2)

    def erase_three(self):
        os.remove(self.options.tmpdir + "/node0/regtest/wallet.dat")
        os.remove(self.options.tmpdir + "/node1/regtest/wallet.dat")
        os.remove(self.options.tmpdir + "/node2/regtest/wallet.dat")

    def advance_time (self, dt=1):
        """Advances mocktime by the given number of seconds."""

        self.time += dt
        set_node_times (self.nodes, self.time)

    def check_monthly(self):
        #Check For Monthly Backups
        tmpdir = self.options.tmpdir
        logging.info("Checking monthly backups are being created")
        path_to_backups = tmpdir+"/node0/regtest/monthlyBackups"
        folder =os.listdir(path_to_backups)
        all_files = [ f for f in folder if os.path.isfile(os.path.join(path_to_backups,f)) ]
        assert_equal(len(all_files),1)
        self.advance_time(3600*24*32)
        for _ in range(3):
            folder =os.listdir(path_to_backups)
            all_files = [ f for f in folder if os.path.isfile(os.path.join(path_to_backups,f)) ]
            if len(all_files) > 1:
                print("Successful monthly backup!")
                self.advance_time(1)
                break
            time.sleep(1.0)
            self.advance_time(1)
        assert_equal(len(all_files),2)

    def create_sendmany_format_for_address(self,addr, reps):
        sendmany_format = {}
        sendmany_format[addr] = {"amount":10.0,"repetitions":reps}
        return sendmany_format

    def split_utxos_for_all_nodes(self):
        for nodeId in range(3):
            node = self.nodes[nodeId]
            assert_equal(node.getbalance(), 1250)
            addr_to_split_to = node.getnewaddress()
            selfmany_format = self.create_sendmany_format_for_address(addr_to_split_to, 124)
            node.sendmany("", selfmany_format)
        self.sync_all()
        self.nodes[3].setgenerate(1)
        self.sync_all()

    def run_test(self):
        logging.info("Generating initial blockchain")
        self.nodes[0].setgenerate( 1)
        sync_blocks(self.nodes)
        self.nodes[1].setgenerate( 1)
        sync_blocks(self.nodes)
        self.nodes[2].setgenerate( 1)
        sync_blocks(self.nodes)
        self.nodes[3].setgenerate( 20)
        sync_blocks(self.nodes)
        assert_equal(self.nodes[3].getbalance(), 0)

        self.split_utxos_for_all_nodes()
        tmpdir = self.options.tmpdir

        # Five rounds of sending each other transactions.
        logging.info("Creating transactions")
        self.fees = Decimal("0.000000")
        for i in range(5):
            self.do_one_round()


        logging.info("Backing up")

        self.nodes[0].backupwallet(tmpdir + "/node0/wallet.bak")
        self.nodes[1].backupwallet(tmpdir + "/node1/wallet.bak")
        self.nodes[2].backupwallet(tmpdir + "/node2/wallet.bak")

        logging.info("More transactions")
        for i in range(5):
            self.do_one_round()

        balance0 = self.nodes[0].getbalance()
        balance1 = self.nodes[1].getbalance()
        balance2 = self.nodes[2].getbalance()
        total = balance0 + balance1 + balance2 + self.fees
        assert_near(total, 3 * 1250,5e-3)

        ##
        # Test restoring spender wallets from backups
        ##
        logging.info("Restoring using wallet.dat")
        self.stop_three()
        self.erase_three()

        # Start node2 with no chain
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/blocks")
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/chainstate")

        # Restore wallets from backup
        shutil.copyfile(tmpdir + "/node0/wallet.bak", tmpdir + "/node0/regtest/wallet.dat")
        shutil.copyfile(tmpdir + "/node1/wallet.bak", tmpdir + "/node1/regtest/wallet.dat")
        shutil.copyfile(tmpdir + "/node2/wallet.bak", tmpdir + "/node2/regtest/wallet.dat")

        logging.info("Re-starting nodes")
        self.start_three()
        sync_blocks(self.nodes)

        assert_equal(self.nodes[0].getbalance(), balance0)
        assert_equal(self.nodes[1].getbalance(), balance1)
        assert_equal(self.nodes[2].getbalance(), balance2)

        logging.info("Restoring using dumped wallet")
        self.stop_three()
        self.erase_three()

        #start node2 with no chain
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/blocks")
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/chainstate")

        self.start_three()

        assert_equal(self.nodes[0].getbalance(), 0)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 0)

        #Check For Monthly Backups
        self.check_monthly()

if __name__ == '__main__':
    WalletBackupTest().main()
