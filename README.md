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
./dsrp-reflector -f dsrp-reflector.ini # read settings from a config file
./dsrp-reflector -p 20010 -c N0MJS -v  # command-line overrides
```

Command-line flags (`-b` address, `-p` port, `-c` callsign, `-v` verbose) take
precedence over the config file.

## Configuration

Settings can be supplied via an INI file (`-f`). See
[dsrp-reflector.ini](dsrp-reflector.ini) for the full annotated sample:

| Section      | Key            | Meaning                                              |
|--------------|----------------|------------------------------------------------------|
| `Network`    | `Address`      | Bind address (`0.0.0.0`, `::`, or a specific IP)     |
| `Network`    | `Port`         | UDP listen port (match each MMDVMHost `GatewayPort`) |
| `Reflector`  | `Callsign`     | Callsign in the link-status reply (≤8 chars)         |
| `Reflector`  | `StatusText`   | Display text on the repeater (≤20 chars)             |
| `Reflector`  | `StatusReply`  | Reply to polls so the repeater shows as linked       |
| `Timing`     | `ClientTimeout`| Seconds of silence before dropping a repeater        |
| `Timing`     | `TalkerTimeout`| Milliseconds before releasing a stalled transmission |
| `Log`        | `Debug`        | Verbose logging                                      |

## How a repeater connects

Point the `[D-Star Network]` section of each repeater's `MMDVM.ini` at the
reflector's address and port (default DSRP gateway port `20010`); no gateway
helper is needed.

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
