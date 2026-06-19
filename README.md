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

## Status

Early design stage. See [DESIGN.md](DESIGN.md) for the protocol analysis,
bit-rate figures, and reflector behavior.

## How a repeater connects

Point the `[D-Star Network]` section of each repeater's `MMDVM-Host.ini` at the
reflector's address and port (default DSRP gateway port `20010`); no gateway
helper is needed.
