# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2014-2015 The Dash developers
# Copyright (c) 2015-2017 The PIVX Developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Helpful routines for regression testing
#

from decimal import Decimal, ROUND_DOWN
import json
import os
import random
import shutil
import subprocess
import time
import re

from authproxy import AuthServiceProxy, JSONRPCException
from util import *

cli_timeout = 30
portSeedsByPID = {}

def set_cli_timeout(updated_timeout = None):
    if updated_timeout:
        cli_timeout = updated_timeout

def set_port_seed(port_seed):
    portSeedsByPID[os.getpid()] = port_seed

def p2p_port(n):
    return 11000 + n + 20 * ( portSeedsByPID[os.getpid()] % 100)

def rpc_port(n):
    return 13000 + n + 20 * ( portSeedsByPID[os.getpid()] % 100)

def check_json_precision():
    """Make sure json library being used does not lose precision converting BTC values"""
    n = Decimal("20000000.00000003")
    satoshis = int(json.loads(json.dumps(float(n)))*1.0e8)
    if satoshis != 2000000000000003:
        raise RuntimeError("JSON encode/decode loses precision")

def reconnect_all(rpc_connections):
    while True:
        for x in range(len(rpc_connections)):
            for y in range(len(rpc_connections)):
                if x != y:
                    while not any([rpc_connections[x].getpeerinfo()]):
                        connect_nodes(rpc_connections[x],y)
                        time.sleep(0.1)
        if all([ conn.getpeerinfo() for conn in rpc_connections if conn ]):
            break
        else:
            print("Retrying connections...")
            time.sleep(0.1)


def sync_blocks(rpc_connections, timeout=None):
    """
    Wait until everybody has the same block count
    """
    while True:
        counts = [ x.getblockcount() for x in rpc_connections if x ]
        if counts == [ counts[0] ]*len(counts):
            return True
        if timeout and timeout > 0:
            timeout -= 0.1
        elif timeout:
            return False
        time.sleep(0.1)

def sync_mempools(rpc_connections):
    """
    Wait until everybody has the same transactions in their memory
    pools
    """
    while True:
        pool = set(rpc_connections[0].getrawmempool())
        num_match = 1
        for i in range(1, len(rpc_connections)):
            if not rpc_connections[i]:
                num_match += 1
            elif set(rpc_connections[i].getrawmempool()) == pool:
                num_match += 1
        if num_match == len(rpc_connections):
            break
        time.sleep(0.1)

bitcoind_processes = {}

def initialize_datadir(dirname, n):
    datadir = os.path.join(dirname, "node"+str(n))
    if not os.path.isdir(datadir):
        os.makedirs(datadir)
    regtest = os.path.join(datadir, "regtest")
    if not os.path.isdir(regtest):
        os.makedirs(regtest)
    with open(os.path.join(datadir, "divi.conf"), 'w') as f:
        f.write("allowunencryptedwallet=1\n")
        f.write("regtest=1\n")
        f.write("rpcuser=rt\n")
        f.write("rpcpassword=rt\n")
        f.write("port="+str(p2p_port(n))+"\n")
        f.write("rpcport="+str(rpc_port(n))+"\n")
    return datadir

def drop_wallet(tmpdir,n):
    os.remove(tmpdir + "/node"+str(n)+"/regtest/wallet.dat")

def prune_datadir(tmpdir,n):
    os.remove(tmpdir + "/node"+str(n)+"/regtest/mncache.dat")
    os.remove(tmpdir + "/node"+str(n)+"/regtest/mnpayments.dat")
    os.remove(tmpdir + "/node"+str(n)+"/regtest/peers.dat")
    os.remove(tmpdir + "/node"+str(n)+"/regtest/netfulfilled.dat")
    shutil.rmtree(tmpdir + "/node"+str(n)+"/regtest/blocks")
    shutil.rmtree(tmpdir + "/node"+str(n)+"/regtest/chainstate")

def _rpchost_to_args(rpchost):
    '''Convert optional IP:port spec to rpcconnect/rpcport args'''
    if rpchost is None:
        return []

    match = re.match('(\[[0-9a-fA-f:]+\]|[^:]+)(?::([0-9]+))?$', rpchost)
    if not match:
        raise ValueError('Invalid RPC host spec ' + rpchost)

    rpcconnect = match.group(1)
    rpcport = match.group(2)

    if rpcconnect.startswith('['): # remove IPv6 [...] wrapping
        rpcconnect = rpcconnect[1:-1]

    rv = ['-rpcconnect=' + rpcconnect]
    if rpcport:
        rv += ['-rpcport=' + rpcport]
    return rv

def start_node(i, dirname, extra_args=None, mn_config_lines=[], rpchost=None, daemon_name = None):
    """
    Start a divid and return RPC connection to it
    """
    datadir = os.path.join(dirname, "node"+str(i))
    with open(os.path.join(datadir, "regtest", "masternode.conf"), "w") as f:
      f.write("\n".join(mn_config_lines))
    binary = []
    runner_name = None
    timeout_limit = cli_timeout
    if os.getenv("RUNNER") is not None:
      runner_name = os.getenv("RUNNER")
      timeout_limit += 30.0
      binary.append(runner_name)
      if os.getenv("RUNNER_FLAGS") is not None:
        flags = str(os.getenv("RUNNER_FLAGS")).split(" ")
        print("Using flags: '{}'".format(flags))
        for flag in flags:
            binary.append(flag)
    divid_env_name = os.getenv("BITCOIND", "divid") if daemon_name is None else daemon_name
    binary.append(divid_env_name)
    # By default, Divi checks if Tor is running on the system and if it is,
    # then the real Tor instance will be used as proxy for .onion
    # connections even if -proxy is set otherwise, and it will try to set up
    # a hidden service listening.  To avoid this behaviour (which we don't want
    # in tests), we turn off Tor control with -nolistenonion.
    args = binary + ["-datadir="+datadir, "-keypool=1", "-discover=0", "-rest", "-nolistenonion"]
    if extra_args is not None: args.extend(extra_args)
    bitcoind_processes[i] = subprocess.Popen(args)
    devnull = open("/dev/null", "w+")
    subprocess.check_call([ os.getenv("BITCOINCLI", "divi-cli"), "-datadir="+datadir] +
                          _rpchost_to_args(rpchost)  +
                          ["-rpcwait", "getblockcount"],
                          stdout=devnull,
                          timeout=timeout_limit)
    devnull.close()
    url = "http://rt:rt@"
    if rpchost and re.match(".*:\\d+$", rpchost):
      url += rpchost
    elif rpchost:
      url += "%s:%d" % (rpchost, rpc_port(i))
    else:
      url += "127.0.0.1:%d" % rpc_port(i)
    proxy = AuthServiceProxy(url,timeout=cli_timeout)
    proxy.url = url # store URL on proxy for info
    return proxy

def start_nodes(num_nodes, dirname, extra_args=None, mn_config_lines=None, rpchost=None):
    """
    Start multiple divids, return RPC connections to them
    """
    if extra_args is None: extra_args = [ None for i in range(num_nodes) ]
    if mn_config_lines is None: mn_config_lines = [[]] * num_nodes
    return [ start_node(i, dirname, extra_args[i], mn_config_lines[i], rpchost) for i in range(num_nodes) ]

def log_filename(dirname, n_node, logname):
    return os.path.join(dirname, "node"+str(n_node), "regtest", logname)

def stop_node(node, i):
    node.stop()
    bitcoind_processes[i].wait()
    del bitcoind_processes[i]

def stop_nodes(nodes):
    for node in nodes:
        if node:
            node.stop()
    del nodes[:] # Emptying array closes connections as a side effect

def set_node_times(nodes, t):
    for node in nodes:
        if node:
            node.setmocktime(t)

def wait_bitcoinds():
    # Wait for all bitcoinds to cleanly exit
    for bitcoind in bitcoind_processes.values():
        bitcoind.wait()
    bitcoind_processes.clear()

def connect_nodes(from_connection, node_num):
    ip_port = "127.0.0.1:"+str(p2p_port(node_num))
    try:
        from_connection.addnode(ip_port, "onetry")
    except Exception as e:
        print("Unable to connect to peer {} through {}: {}".format(node_num,ip_port,e))
        raise
    # poll until version handshake complete to avoid race conditions
    # with transaction relaying
    while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
        time.sleep(0.1)

def connect_nodes_bi(nodes, a, b):
    connect_nodes(nodes[a], b)
    connect_nodes(nodes[b], a)

def find_output(node, txid, amount):
    """
    Return index to output of txid with value amount
    Raises exception if there is none.
    """
    txdata = node.getrawtransaction(txid, 1)
    for i in range(len(txdata["vout"])):
        if txdata["vout"][i]["value"] == amount:
            return i
    raise RuntimeError("find_output txid %s : %s not found"%(txid,str(amount)))

def gather_inputs(from_node, amount_needed, confirmations_required=1):
    """
    Return a random set of unspent txouts that are enough to pay amount_needed
    """
    assert(confirmations_required >=0)
    utxo = from_node.listunspent(confirmations_required)
    random.shuffle(utxo)
    inputs = []
    total_in = Decimal("0.00000000")
    while total_in < amount_needed and len(utxo) > 0:
        t = utxo.pop()
        total_in += t["amount"]
        inputs.append({ "txid" : t["txid"], "vout" : t["vout"], "address" : t["address"] } )
    if total_in < amount_needed:
        raise RuntimeError("Insufficient funds: need %d, have %d"%(amount_needed, total_in))
    return (total_in, inputs)

def make_change(from_node, amount_in, amount_out, fee):
    """
    Create change output(s), return them
    """
    outputs = {}
    amount = amount_out+fee
    change = amount_in - amount
    if change > amount*2:
        # Create an extra change output to break up big inputs
        change_address = from_node.getnewaddress()
        # Split change in two, being careful of rounding:
        outputs[change_address] = Decimal(change/2).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)
        change = amount_in - amount - outputs[change_address]
    if change > 0:
        outputs[from_node.getnewaddress()] = change
    return outputs

def assert_equal(thing1, thing2):
    if thing1 != thing2:
        raise AssertionError("%s != %s"%(str(thing1),str(thing2)))

def assert_greater_than(thing1, thing2):
    if thing1 <= thing2:
        raise AssertionError("%s <= %s"%(str(thing1),str(thing2)))

def assert_strict_within(value, lower, upper):
  assert_greater_than(upper,value)
  assert_greater_than(value,lower)

def assert_near(value, target, tolerance):
  assert_strict_within(value, target-tolerance, target+tolerance)

def assert_raises(exc, fun, *args, **kwds):
    try:
        fun(*args, **kwds)
    except exc:
        pass
    except Exception as e:
        raise AssertionError("Unexpected exception raised: "+type(e).__name__)
    else:
        raise AssertionError("No exception raised")
