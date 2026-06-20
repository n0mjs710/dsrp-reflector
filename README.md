# dsrp-reflector

A minimal, dependency-free C11 reflector ("party line") for MMDVMHost D-Star
repeaters. Whatever one connected repeater transmits is relayed to all the
others.

This is **not** a connection to the real D-Star network. There is no
DExtra/DPlus/DCS/G2 internetworking, no callsign routing, and no gateway. The
reflector simply replaces the local gateway helper that MMDVMHost normally talks
to, and relays the private **DSRP** UDP protocol between every connected
repeater.

```
  repeater A ─DSRP─┐
  repeater B ─DSRP─┤──►  dsrp-reflector  ──► fan-out to all the others
  repeater C ─DSRP─┘
```

See [DESIGN.md](DESIGN.md) for the protocol analysis, bit-rate figures, and
reflector behavior.

## Build

```
make
```

Dependency-free C11; needs only a POSIX libc. Produces `./dsrp-reflector`.

## Run

```
./dsrp-reflector                       # defaults: bind 0.0.0.0:20010, callsign DSRP
./dsrp-reflector -c dsrp-reflector.ini # read settings from a config file
./dsrp-reflector -p 20010 -C N0MJS -v  # command-line overrides
```

Command-line flags (`-c` config file, `-b` address, `-p` port, `-C` callsign,
`-v` verbose) take precedence over the config file.

## Configuration

Settings can be supplied via an INI file (`-c`). See
[dsrp-reflector.ini](dsrp-reflector.ini) for the full annotated sample:

| Section      | Key            | Meaning                                              |
|--------------|----------------|------------------------------------------------------|
| `Network`    | `Address`      | Bind address (`0.0.0.0`, `::`, or a specific IP)     |
| `Network`    | `Port`         | UDP listen port (match each MMDVMHost `GatewayPort`) |
| `Reflector`  | `Callsign`     | Callsign in the link-status reply (≤8 chars)         |
| `Reflector`  | `StatusText`   | Display text on the repeater (≤20 chars)             |
| `Reflector`  | `StatusReply`  | Reply with a link-status packet so the repeater shows as linked |
| `Reflector`  | `StatusInterval`| Status resend cadence in seconds; `0` = on connect only |
| `Timing`     | `ClientTimeout`| Seconds of silence before dropping a repeater        |
| `Timing`     | `TalkerTimeout`| Milliseconds before releasing a stalled transmission |
| `Log`        | `Debug`        | Verbose logging                                      |
| `Log`        | `RosterInterval`| Log connected repeaters every N seconds; `0` = off (default 300) |

## Monitoring connected repeaters

Connects, disconnects, and each transmission are logged as they happen. In
addition, every `RosterInterval` seconds (default 300; set `0` to disable) the
reflector logs the full list of currently connected repeaters — one per line,
with source `IP:port`, the repeater callsign, the last-heard user, and the
repeater's poll version string — so an operator watching the journal can see at
a glance who is and isn't connected:

```
I: connected repeaters: 2
I:   198.51.100.7:20011      WB2XYZ B  last heard: N0MJS     [linux_mmdvm-20210101]
I:   203.0.113.4:20011       W0XYZ  B  last heard: W0XYZ     [linux_mmdvm-20210101]
```

The DSRP poll carries no callsign, so both callsigns are learned from the
D-Star header the first time a repeater is keyed: the **repeater** column is
`RPT1` (the repeater/module itself), and **last heard** is `MYCALL1` (the user
who last transmitted through it). Until a repeater has been keyed at least once
both read `-`, though its address is already known from its polls. Under systemd
the lines land in the journal: `journalctl -u dsrp-reflector -f`.

## How a repeater connects

In each repeater's `MMDVM.ini`:

- `[D-Star Network]` — point it at the reflector; no gateway helper is needed:
  - `Enable=1`
  - `GatewayAddress=<reflector IP>`
  - `GatewayPort=20010` (the reflector's listen port)
  - `LocalAddress=0.0.0.0` — **change this from the default `127.0.0.1`.** It is
    the local bind for MMDVMHost's socket, not the reflector address. The
    `127.0.0.1` default assumes a gateway on the same machine; for a remote
    reflector use `0.0.0.0` (or blank, or this host's LAN IP). Loopback cannot
    reach a remote reflector.
  - `LocalPort=20011` (where this MMDVMHost receives; default is fine)
- `[D-Star]` — set **`RemoteGateway=1`**.

A repeater behind NAT needs no port forwarding: it initiates to the reflector,
and the reflector always replies to the observed source address, so the NAT
mapping (kept alive by the 60 s polls) carries the return traffic. Only the
reflector's `20010` must be reachable.

`RemoteGateway=1` is important for this topology. The reflector relays each
transmission's header verbatim, including the *originating* repeater's RPT1/RPT2
callsign. With `RemoteGateway=1`, each receiving MMDVMHost rewrites those fields
to its **own** callsign before transmitting, so every node correctly identifies
as itself on the air. Without it, repeaters would transmit the sender's callsign
in the RPT fields (audio still works, but the on-air ID would be wrong).

## Install

```
make
sudo make install
sudo systemctl enable --now dsrp-reflector
```

`make install` copies the binary to `/usr/local/bin`, the config to
`/etc/dsrp-reflector.ini` (an existing config is preserved; the sample is
written as `dsrp-reflector.ini.sample` instead), and the systemd unit to
`/lib/systemd/system`, creating an unprivileged `dsrp` service user. Paths are
overridable (`PREFIX`, `BINDIR`, `SYSCONFDIR`, `UNITDIR`, `DESTDIR`).
`sudo make uninstall` reverses it, leaving the config in place.

The systemd unit is [systemd/dsrp-reflector.service](systemd/dsrp-reflector.service).
