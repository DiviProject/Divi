# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Base class for RPC testing

import os
import shutil
import sys
import tempfile
import traceback

from util import *
from authproxy import JSONRPCException


class BitcoinTestFramework(object):

    # These may be over-ridden by subclasses:
    def run_test(self):
        for node in self.nodes:
            assert_equal(node.getblockcount(), 200)
            assert_equal(node.getbalance(), 25*50)

    def add_options(self, parser):
        pass

    def setup_chain(self,number_of_nodes=4):
        print("Initializing test directory "+self.options.tmpdir)
        for i in range(number_of_nodes):
            datadir=initialize_datadir(self.options.tmpdir, i)

    def setup_nodes(self):
        return start_nodes(4, self.options.tmpdir)

    def setup_network(self, split = False):
        self.nodes = self.setup_nodes()

        # Connect the nodes as a "chain".  This allows us
        # to split the network between nodes 1 and 2 to get
        # two halves that can work on competing chains.

        # If we joined network halves, connect the nodes from the joint
        # on outward.  This ensures that chains are properly reorganised.
        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        """
        Split the network of four nodes into nodes 0/1 and 2/3.
        """
        assert not self.is_network_split
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(True)

    def sync_all(self):
        if self.is_network_split:
            sync_blocks(self.nodes[:2])
            sync_blocks(self.nodes[2:])
            sync_mempools(self.nodes[:2])
            sync_mempools(self.nodes[2:])
        else:
            sync_blocks(self.nodes)
            sync_mempools(self.nodes)

    def join_network(self):
        """
        Join the (previously split) network halves together.
        """
        assert self.is_network_split
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

    def main(self):
        import optparse

        parser = optparse.OptionParser(usage="%prog [options]")
        parser.add_option("--nocleanup", dest="nocleanup", default=False, action="store_true",
                          help="Leave bitcoinds and test.* datadir on exit or error")
        parser.add_option("--srcdir", dest="srcdir", default="../../src",
                          help="Source directory containing bitcoind/bitcoin-cli (default: %default%)")
        parser.add_option("--prior_binaries", dest="prior_binaries", default="../../prior_binaries",
                    help="Source directory containing bitcoind/bitcoin-cli (default: %default%)")
        parser.add_option("--tmpdir", dest="tmpdir", default="",
                          help="Root directory for datadirs")
        parser.add_option("--tracerpc", dest="trace_rpc", default=False, action="store_true",
                          help="Print out all RPC calls as they are made")
        parser.add_option("--portseed", dest="port_seed", default=os.getpid(), type=int,
                          help="The seed to use for assigning port numbers (default: current process id)")
        parser.add_option("--cli_timeout", dest="cli_timeout", default=60.0, type=float,
                          help="The rpc timeout to use for making rpc calls to a node instance")
        self.add_options(parser)
        (self.options, self.args) = parser.parse_args()

        # We do not want to set the default value to a new temporary folder
        # explicitly in the parser, as that will then always create the
        # directory (even if another one is specified).  Hence we use an empty
        # default value, and only create one if needed.
        if not self.options.tmpdir:
            self.options.tmpdir = tempfile.mkdtemp(prefix="test")

        if self.options.trace_rpc:
            import logging
            logging.basicConfig(level=logging.DEBUG)

        import util
        util.set_port_seed(self.options.port_seed)
        util.set_cli_timeout(self.options.cli_timeout)

        os.environ['PATH'] = self.options.prior_binaries+":"+self.options.srcdir+":"+os.environ['PATH']

        check_json_precision()

        success = False
        try:
            if not os.path.isdir(self.options.tmpdir):
                os.makedirs(self.options.tmpdir)
            self.setup_chain()

            self.setup_network()

            self.run_test()

            success = True

        except JSONRPCException as e:
            print("JSONRPC error: "+e.error['message'])
            traceback.print_tb(sys.exc_info()[2])
        except AssertionError as e:
            print("Assertion failed: %s"%e)
            traceback.print_tb(sys.exc_info()[2])
        except Exception as e:
            print("Unexpected exception caught during testing: "+str(e))
            traceback.print_tb(sys.exc_info()[2])

        if not self.options.nocleanup:
            print("Cleaning up")
            stop_nodes(self.nodes)
            wait_bitcoinds()
            shutil.rmtree(self.options.tmpdir)

        if success:
            print("Tests successful")
            sys.exit(0)
        else:
            print("Failed")
            sys.exit(1)
