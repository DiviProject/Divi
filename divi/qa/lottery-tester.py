#!/usr/bin/python

import sys
import argparse
import subprocess
import json
import hashlib
import os.path

verbose = False
COIN = 100000000

def getrawtransaction(executor, transactionHash):
    return json.loads(executor(['getrawtransaction', transactionHash, '1']))

def getblock(executor, blockhash):
    return json.loads(executor(['getblock', blockhash]))

def getblockhash(executor, height):
    return executor(['getblockhash', str(height)])


def executeProcess(args):
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    p.wait()
    out, err = p.communicate()
    return out

def getcoinstakehash(block):
    vtx = block['tx']
    return vtx[1] if len(vtx) > 1 else vtx[0]

def compareHashes(left, right):
    int_left = int(left, 16)
    int_right = int(right, 16)
    if int_left < int_right:
        return 1
    elif int_left > int_right:
        return -1
    else:
        return 0

def isLotteryTicketValid(coinstake, minValue):
    vout = coinstake['vout']
    totalStaked = vout[0]['valueSat']
    if len(vout) > 1:
        payee = vout[1]['scriptPubKey']['hex']

        sameAddressTxOut = lambda right: payee == right['scriptPubKey']['hex']

        def helper(accum, txout):
            value = 0
            if sameAddressTxOut(txout):
                value = txout['valueSat']
            return accum + value

        totalStaked = reduce(helper, vout, 0)

    return totalStaked > minValue * COIN

def calculateLotteryScore(coinstakeHash, lastLotteryHash):
    sha256 = hashlib.sha256()
    sha256.update(coinstakeHash)
    sha256.update(lastLotteryHash)
    return sha256.hexdigest()

def findWinners(executor, lotteryBlockHeight, lotteryCycle):
    startBlock = lotteryBlockHeight - lotteryCycle
    lastLotteryHash = getblockhash(executor, startBlock)
    winners = []

    print 'Scanning from block {} to {}'.format(startBlock, lotteryBlockHeight)

    for i in xrange(startBlock, lotteryBlockHeight):
        blockHash = getblockhash(executor, i)
        block = getblock(executor, blockHash)
        coinstakeHash = getcoinstakehash(block)
        coinstake = getrawtransaction(executor, coinstakeHash)

        if verbose:
            print 'Checking coinstake: {}, blockHeight: {}'.format(coinstakeHash, i)

        if isLotteryTicketValid(coinstake, 10000) == False:
            if verbose:
                print 'Coinstake: {} not valid for lottery'.format(coinstakeHash)

            continue

        lotteryScore = calculateLotteryScore(coinstakeHash, lastLotteryHash)

        if verbose:
            print 'Lottery score {} for coinstake {}'.format(lotteryScore, coinstakeHash)

        winners.append([lotteryScore, coinstake['vout'][1]['scriptPubKey']['hex']])

    winners.sort(lambda left, right: int(left[0], 16) > int(right[0], 16))

    return winners[:11]

def checkWinner(executableName, lotteryBlockHeight, lotteryCycle, lotteryPart):

    executor = lambda args: executeProcess([executableName] + args)
    winners = findWinners(executor, lotteryBlockHeight, lotteryCycle)
    lotteryPool = lotteryPart * lotteryCycle * COIN

    lotteryBlock = getblock(executor, getblockhash(executor, lotteryBlockHeight))
    lotteryPaymentTx = getrawtransaction(executor, lotteryBlock['tx'][1])
    vout = lotteryPaymentTx['vout']
    print  'Total lottery pool: {}'.format(lotteryPool)
    actualValues = map(lambda out: (out['scriptPubKey']['hex'], out['valueSat']), vout)

    winnersWithRewards = zip(winners, [lotteryPool / 2] + [lotteryPool / 20 for i in xrange(0, 10)])

    if verbose:
        print 'Winners: {}'.format(winnersWithRewards)

    invalidWinners = 0

    for winner in winnersWithRewards:
        found = False
        for actualValue in actualValues:
            if actualValue[0] == winner[0][1] and actualValue[1] == winner[1]:
                found = True
                break

        if found == False:
            print 'Invalid payment, expected payment to address {} with reward {}, got {} with reward {}'.format(
                winner[0], winner[1], actualValue[0], actualValue[1]
            )
            invalidWinners += 1

    if invalidWinners == 0:
        print 'Correct lottery payment'
    else:
        print 'Invalid lottery payment'

def main(argv):

    parser = argparse.ArgumentParser()
    parser.add_argument('-v', dest='verbose', action='store_true', help='verbose output')
    parser.add_argument('--lottery-part', dest='lottery_part', help='value that represents how many coins will be sent '
                                                                    'to lottery for every block', default=50)
    parser.add_argument('lottery_height', metavar='lottery_block_height', type=int,
                        help='height of block where to check lottery')
    parser.add_argument('lottery_cycle', metavar='lottery_cycle', type=int,
                        help='length of lottery cycle in blocks')
    parser.add_argument('--prefix', dest='prefix', help='path to folder where divi-cli is located', default='.')
    args = parser.parse_args()
    lotteryCycle = args.lottery_cycle
    lotteryBlockHeight = args.lottery_height
    lotteryPart = args.lottery_part
    prefix = args.prefix
    global verbose
    verbose = args.verbose

    if lotteryBlockHeight < lotteryCycle:
        raise ValueError('lottery height: {} is smaller than lottery cycle: {}'.format(lotteryBlockHeight, lotteryCycle))

    executableName = prefix + '/divi-cli'
    print executableName

    if os.path.isfile(executableName) == False:
        print 'Cannot find divi-cli, consider using --prefix'
        sys.exit(0)

    try:
        json.loads(executeProcess([executableName, 'getinfo']))
    except ValueError:
        print 'Divi-cli is not running, or it is in wrong dir'
        sys.exit(0)

    checkWinner(executableName, lotteryBlockHeight, lotteryCycle, lotteryPart)


if __name__ == "__main__":
   main(sys.argv[1:])