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

After you have the docker container built and are sshed into the Docker container. You can run `sh atomic.reg.test.sh` in this same folder (`/src/atomic-swap`). You should be able to run regression tests from there.

Please note that the regression tests only include the initiate function, simply due to the fact that the current state of the cli does not make it easy to assign contract hashes to variables. Ideally once we have RPC/HTTP endpoints ready. We can regression tests the entire thing in something such as `Mocha` or anything that supports HTTP.

### References

- https://github.com/decred/atomicswap
