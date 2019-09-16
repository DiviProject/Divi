### Notes about Atomic Swaps

In order to compile Atomic Swaps. You will need to have `go >=1.11`.

In order to compile scripts with `go build`. You will need to install the following golang modules.

```bash
go get -u github.com/btcsuite/btcd/txscript
go get -u github.com/btcsuite/btcd/wire
go get -u github.com/btcsuite/btcd/chaincfg
go get -u github.com/btcsuite/btcd/chaincfg/chainhash
go get -u github.com/btcsuite/btcd/rpcclient
go get -u github.com/btcsuite/btcutil
go get -u github.com/btcsuite/btcwallet/wallet/txrules
```

You can also just run `config.sh` or `config.bat` to get all the required dependencies setup.

After you have the required dependencies you can then run `go build` and configure the atomic swaps.

The commands reflect the same functionality found in Decred's original source code:

```bash
Commands:
  initiate <participant address> <amount>
  participate <initiator address> <amount> <secret hash>
  redeem <contract> <contract transaction> <secret>
  refund <contract> <contract transaction>
  extractsecret <redemption transaction> <secret hash>
  auditcontract <contract> <contract transaction>
```

### Testing

Make sure to setup the Dockerfile from `/test/docker/atomic.test.Dockerfile`.

```bash
docker build -t atomicdivi -f ./test/docker/atomic.test.Dockerfile .
```

You can learn more about debugging atomic swaps with [ATOMIC.ENV.MD](../../test/docker/ATOMIC.ENV.MD).

### References

- https://github.com/decred/atomicswap
