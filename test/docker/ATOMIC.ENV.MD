### Notes about the Atomic Docker Environment

The Docker environment provided for Atomic Swaps is intended to test trades between a Divi and Bitcoin regression testing node. Ideally when building this environment, you are running it in the root directory.

```bash
docker build -t atomicdivi -f ./test/docker/atomic.test.Dockerfile .
```

Once the Docker environment is built, you can then SSH into it and start testing out both Bitcoin and Divi regression test nodes.

```bash
docker run atomicdivi --itd
docker attach [CONTAINER_ID]
```

You can run both regression test nodes by doing the following.

```bash
# Run divi regtest node
cd src
divid -regtest -daemon
# Run bitcoin regtest node
cd ../bitcoin/src
bitcoind -regtest -daemon
```

### References

- https://bitcoin.org/en/developer-examples#regtest-mode
