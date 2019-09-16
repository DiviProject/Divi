## Docker Compilation and Build

Docker is a much easier way to run the project from source in a more predictable way, especially if you are struggling compiling via OSX or Windows. This is the way to go.

### Requirements

1. Docker CE v2.0.0 or better

2. Linux containers enabled with Docker

### Compilation

```bash
docker build -t divi .
docker run divi
```

### Configuration

If you need to interact with external services in a dev environment. The following will expose all ports to the local machine.

```bash
docker run divi --publish-all=true
```

For production, it's suggested to just expose the necessary ports.

```bash
docker run divi --expose 1337
```

If you want to SSH into the docker container for development purposes you can run.

```bash
docker exec -itd divi bash
```
