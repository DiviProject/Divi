## Definition

This document describes a few way how to run Divi client persistently remotely(on VPS).

### Options

We will imply that user is connected via ssh to remote host.

1. Simply running `# /path/to/divid/divid`
2. Running as daemon `# /path/to/divid/divid -daemon` or placing daemon=1 into `divi.conf`
3. Running as service(systemd, init.d, etc)

Let's breakdown every solution. 

1. This is the worst solution since it runs as blocking process to the terminal and you are pretty much limited to do anything. 
2. This solution is better because it forks process and client runs in background as daemon, in this case you can safely close your connection and everything will work.
3. This solution is by far the best because it does the same as 2) but also supports auto-restart and auto-start. (Recommended)

### Running as service(systemd)

To run client as systemd daemon we need to make simple configuration. 

Unit file can be found at our github repository, `divi/contrib/init/divid.service`

1. Place so `divid.service` unit file into `/etc/systemd/system`. 
2. Create folder for datadir: `sudo mkdir -p /var/lib/divid`
3. Enable unit for auto-start: `sudo systemctl start divid`. (Optional)
4. Start service: `sudo systemctl start divid`
5. Check status: `sudo systemctl status divid`

Useful commands:

* `sudo systemctl status divid` - shows status
* `sudo systemctl stop divid` - stops daemon
* `tail -f /var/lib/divid/debug.log` - show logs

#### Using our divid.service

Our unit file that is provided at `divi/contrib/init/divid.service` has configuration for different from default `divi.conf` and `datadir` folders. 
Default is: 

* `-datadir=/var/lib/divid`
* `-conf=/etc/divi/divi.conf`

Using our unit file won't place `datadir` and `divi.conf` under `~/.divi`. If you want to change those locations, you will need to modify unit file. 