# dsrp-reflector — Design

A minimal, dependency-free reflector ("party line") for MMDVMHost D-Star
repeaters. Whatever one connected repeater transmits is relayed to all the
others. No connection to the real D-Star network (DExtra/DPlus/DCS/G2), no
internetworking, no callsign routing — just a UDP frame relay.

## 1. Where this sits

MMDVMHost does **not** speak the over-the-air D-Star network protocol. Its
`[D-Star Network]` block talks a small private UDP protocol — **DSRP** — to a
local *gateway* helper (normally ircDDBGateway or DStarGateway), which does all
the real internetworking.

```
  repeater A (MMDVMHost) ─DSRP─┐
  repeater B (MMDVMHost) ─DSRP─┤──►  dsrp-reflector  ──► fan-out to all others
  repeater C (MMDVMHost) ─DSRP─┘
```

`dsrp-reflector` **replaces the gateway**. Each repeater points its MMDVMHost
`GatewayAddress`/`GatewayPort` at the reflector. There is no other piece in the
chain — no AMBE decode, no FEC, no callsign DB, no real D-Star format — which
removes the usual sources of added latency, jitter, loss, and failure.

MMDVMHost defaults (`[D-Star Network]`):
- MMDVMHost → gateway: `GatewayPort=20010`
- gateway → MMDVMHost: `LocalPort=20011`

The reflector listens on one UDP socket and sends replies back to the source
address of each repeater (the `LocalPort` each MMDVMHost is bound to).

## 2. DSRP wire format

Every packet begins with ASCII `DSRP` (4 bytes) then a 1-byte type.

| Type        | Name              | Dir       | Size  | Payload after `DSRP`+type                       |
|-------------|-------------------|-----------|-------|-------------------------------------------------|
| `0x20`/`22` | Header (idle/busy)| both      | 49 B  | 2B session id, 1B(0), 41-byte D-Star header     |
| `0x21`/`23` | Voice/data frame  | both      | 21 B  | 2B id, 1B seq, 1B errors, 12-byte frame         |
| `0x0A`      | Poll (keepalive)  | rptr→refl | ~20 B | version string + NUL                            |
| `0x00`      | Link status text  | refl→rptr | ~46 B | status code + 20-char text + 8-char reflector   |

Frame mechanics (verified against MMDVMHost `DStarNetwork.cpp` /
`DStarControl.cpp` and DStarGateway `HBRepeaterProtocolHandler.cpp`):

- **Session id** (2B, offset 5–6): a random 16-bit value the *transmitting*
  MMDVMHost picks per over. The receiver latches it from the header and rejects
  any data frame whose id differs. The reflector relays header + frames with the
  id **untouched**.
- **Sequence** (offset 7): cycles 0→0x14 (21 frames = one 420 ms superframe),
  resetting to 0 on each voice sync. Bit `0x40` = **end-of-transmission (EOT)**.
- **Header sent twice** by the sender for reliability.
- **No callsign validation on receive**: the receiving MMDVMHost transmits a
  network header without checking MYCALL/URCALL/RPT1/RPT2, so the reflector can
  relay headers **verbatim** — no rewriting needed.
- **No handshake / registration**: the gateway's repeater handler classifies
  packets purely by type byte. A repeater "appears" simply by polling.
- **Polls** fire every 60 s. Used by the reflector as connect + keepalive +
  timeout signal per repeater. Replying with a `0x00` status is optional
  (MMDVMHost works without it) but keeps dashboards' link status sane.

## 3. Bit-rate / load

One frame every 20 ms during an active over → 50 packets/sec, 21 B DSRP each.

| Layer                         | Per pkt | ×50/s    | Rate          |
|-------------------------------|---------|----------|---------------|
| DSRP payload                  | 21 B    | 1050 B/s | ~8.4 kbit/s   |
| + UDP/IPv4 (28 B)             | 49 B    | 2450 B/s | ~19.6 kbit/s  |
| + Ethernet L2 framing         | ~87 B   | ~4350 B/s| ~35 kbit/s    |

Header burst (2×49 B) and 60 s polls are negligible. Party-line semantics mean
only one repeater talks at a time; the reflector fans that single stream out to
N−1 others:

- Ingress: ~20 kbit/s (IP layer)
- Egress: (N−1) × ~20 kbit/s — e.g. ~180 kbit/s aggregate for 10 repeaters.

Bandwidth is a non-issue; latency is the only thing worth caring about, and the
relay adds essentially none.

## 4. Reflector behavior

State per connected repeater: source address, last-poll timestamp, (and the
shared notion of who currently holds the channel).

- **Connect**: first poll from a new source address adds the repeater.
- **Keepalive / timeout**: drop a repeater whose last poll exceeds a timeout
  (e.g. 2–3 missed 60 s polls).
- **Talker lock (collision policy)**: first-keyup-wins. When an idle channel
  receives a header, lock to that source's session id. Relay its header + frames
  to all other repeaters. Ignore headers/frames from other sources until the
  lock releases.
- **Release**: on the EOT frame (`0x40`), or a watchdog timeout (~1.5–2 s) if
  the EOT is lost. (MMDVMHost itself uses a 1500 ms net watchdog.)
- **No echo**: never relay a stream back to its originator.
- **Poll reply** (optional): respond to polls with a `0x00` status packet.

## 5. Jitter / loss tolerance — why a single reflector is enough

A reasonable worry: MMDVMHost normally talks to a *local* gateway, so won't a
remote reflector expose it to internet jitter/loss/reordering it can't handle?
We verified the receive paths in both code bases; the answer is no — a single
reflector is no worse than the de facto standard (DStarGateway/ircDDBGateway):

- **MMDVMHost has no classic jitter buffer.** Incoming network frames are pulled
  from a ring buffer each clock tick and pushed toward the modem immediately
  (`DStarNetwork`/`DStarControl::writeNetwork`). The real cushion is the modem
  TX FIFO (~100-200 ms), metered by `hasDStarSpace()`.
- It conceals **loss** by inserting silence/repeat frames keyed on the DSRP
  sequence byte (`insertSilence`), counting them as packet loss; a 300 ms stall
  guard fills longer gaps.
- It is **hostile to reordering**: a late/out-of-order frame computes as a large
  forward sequence gap (mod 21) and is rejected, not resequenced.
- **DStarGateway is a pure relay.** `CRepeaterHandler::process(CAMBEData&)`
  forwards each reflector frame to the repeater the instant it arrives over the
  internet — no buffering, reordering, or concealment. So MMDVMHost is *already*
  fed internet-jittered audio today; the local gateway hop never de-jittered it.

Conclusion: all de-jitter/concealment lives in MMDVMHost + the modem FIFO,
downstream of any gateway, and is identical whether frames arrive via a local
relay gateway or straight from a remote reflector. A single reflector is
therefore "equally challenged," and avoids deploying a second app per repeater.

A future option (not required): a thin *local* gateway proxy could add a real
3-5 frame (60-100 ms) de-jitter + reorder + loss-conceal buffer on the
toward-repeater direction, handing MMDVMHost a pristine localhost stream — a
strict improvement over the status quo. We keep DSRP end-to-end so the reflector
needs no change if we ever add it.

## 6. Implementation notes

- Dependency-free **C11**, same mold as `ipsc2hbpc`: one UDP socket, a small
  fixed-size client table, a monotonic clock for poll/EOT timeouts.
- Pure relay — frames are opaque 12-byte blobs; no decode.
- Single-threaded event loop (recvfrom + timed clock tick) is sufficient given
  the packet rates above.
