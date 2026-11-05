# D133 stream: PC1 → D133 → PC2

Live data goes PC1 → Ethernet → D133 (IP forwarding) → WiFi AP → PC2. The D133 stores nothing.
This is the **live plane** (UDP). Pulling files off the SD card (dataServer, TCP) is the **batch plane**;
the two do not share code or sockets.

## Run it

```
PC2 (receiver)   node server/stream/cli.mjs recv --port 5600
PC1 (sender)     node server/stream/cli.mjs send --to <PC2 address>:5600 --bitrate 4M
                 ... --mode adaptive --max 20M            follows the receiver's reports
                 ... --mode ramp --ramp-start 1M --ramp-step 1M --max 30M    finds where loss begins
                 producer | node server/stream/cli.mjs send --to ... --source stdin    your own frames (u32 length + bytes)
PC1 (board check) node server/stream/cli.mjs calibrate --to <PC2 address>:5600 --board 192.168.1.1
```

The same things from the dashboard: **D133 board → End-to-end stream**. Run the sender's dashboard on PC1:
it shows the whole path because the receiver reports back. The command line tool needs only Node 20 and
two things from this folder, kept at the same relative paths: `server/stream/` and `server/d133.mjs`
(`calibrate` reads the board through it). Copy just those to PC2 if you do not want the dashboard there;
no `npm install` is needed.

No board at hand? `node server/stream/sim/demo.mjs --to 5600 --cable 0.02 --d133 0.08 --wifi 0.03` starts a
simulated path and a simulated board stats port; point the dashboard's Board IP at 127.0.0.1 and its Stats
port at the one printed.

## Wire format (`server/stream/protocol.mjs`)

DATA, 32 byte header: magic 0xD133, version, type, **stream id**, flags, **seq** (u32 per datagram),
frame id, **fragment index / count**, **capture time** (µs since epoch), send delay (µs), then at most
**1400 payload bytes**. A datagram is at most 1432 bytes, 1460 with UDP and IPv4 headers: inside the
1500 byte MTU, so neither the PCs nor the D133 ever have to fragment.
REPORT (receiver → sender, 80 bytes, every 500 ms): expected / received / lost / reordered / duplicates,
jitter, frames ok / dropped, queue drops, datagrams and bytes received, highest seq, the echoed send time.

## Where each sentence of the design went

"Verified" says where it was checked: **node** = the Node tests in `server/stream/test` (real UDP on loopback),
**sim** = against the simulated D133 in `sim/path.mjs`, **host C** = the board code built for the PC,
**RV32** = cross-compiled with the Luban-Lite flags (compiles, has not run), **board** = needs the real board.

| Design statement | Code | Verified |
|---|---|---|
| Live stream and file pull are separate planes | `server/stream/*` (UDP) vs `rtthread/dataserver.c` (TCP); two sections in `D133Board.tsx`; the engine never touches the pull | node `engine.test` (one run at a time), python `test_strata_d133` |
| The live stream uses UDP, not TCP | `sender.mjs`, `receiver.mjs` use `node:dgram` only; there is no TCP path for stream data | node `stream.test` |
| UDP: lost is lost, the receiver carries on with the newest; nothing is retransmitted | no resend path anywhere in `sender.mjs`; the protocol has no ack/nack; the receiver drops what is incomplete | node `receiver.test` (incomplete frame dropped), `stream.test` |
| Header: stream id, sequence number, capture time, fragment index | `protocol.mjs` (`encodeData` / `decode`) | node `protocol.test` |
| Payload ≤ 1400 bytes (no IP fragmentation) | `MAX_PAYLOAD`; `encodeData` throws, `decode` refuses larger | node `protocol.test`, `stream.test` (largest datagram seen on the path ≤ 1432) |
| The receiver reassembles by frame | `receiver.mjs` `assemble()` | node `receiver.test` |
| An incomplete frame is dropped at once, nothing waits | `tolerateFrames = 0`: dropped when a newer frame starts; `frameTimeoutMs` bounds the last frame | node `receiver.test` (dropped on the next frame; dropped after the timeout, not before) |
| Sequence numbers give exact loss and reordering | `receiver.mjs` `track()` (window, wrap-safe) | node `receiver.test` (exact loss, reordered, duplicates, wrap, too-late), `stream.test` (receiver's loss equals what the path dropped) |
| The receiver reports periodically: received, lost, jitter | `receiver.mjs` `tick()` → REPORT | node `receiver.test`, `stream.test` |
| The sender lowers bitrate (or quality) from the reports | `adapt.mjs`; `sender.mjs` emits `adapt` and `rate` events; the dashboard's generator shrinks its frames | node `control.test`, `stream.test` (adaptive on a limited path; external producer receives the new rate) |
| A worse WiFi must not just make delay grow | `adapt.mjs`: loss ⇒ lower, no reports ⇒ back off (`onSilence`) | node `control.test`, `stream.test` (no receiver ⇒ rate falls) |
| Queues have a limit and drop the oldest: sender, receiver, D133 | sender frame queue and receiver frame queue (`queue.mjs`, plus `maxOpenFrames`); D133: the lwIP pools are bounded by config and their use is shown | node `queue.test`, `stream.test` (both queues keep the newest) ; D133 side: see next rows |
| "The D133 forwards in one tcpip thread, no hardware offload" | **not asserted in code.** It was an inference from lwIP's structure, not something the repository states. It is made measurable instead: forwarded and refused counts, and the rate-limit search | board |
| How much the AIC8800 WiFi carries depends on the chip and band | not assumed anywhere; measured by **Find the limit** and `calibrate` | board |
| A burst overflows the D133's forwarding queue | the sender spreads a frame's fragments over `pacing` (0.5) of the frame interval; `netmon.c` counts a frame `netif->input` refused as `rx_dropped` (lwIP itself counts nothing when the tcpip mailbox is full) | pacing: node (it runs); counting: host C + RV32 (compiles); its **effect on the board is unmeasured** |
| With RT_LWIP_STATS the drop counters are visible | `Kconfig` `DATASERVER_LWIP_STATS` selects `RT_LWIP_STATS`; `lwipopts.h` makes the counters 32 bit; `netmon_lwip()` → `dsstat` JSON `lwip` object → dashboard "lwIP on the board" | host C `test_dsstat.py` (every field, worst-case size), RV32 compile with stats on; values on the board: board |
| Tune pbuf count and tcpip mailbox only after measuring | **nothing was changed**: `PBUF_POOL_SIZE`, `MEMP_NUM_TCPIP_MSG_INPKT`, `TCPIP_MBOX_SIZE` are as before. The dashboard shows each limit beside its high-water mark and failure count, with that advice | `git diff` on `lwipopts.h` shows only the stats block |
| Run iperf first: where does loss begin | **Find the limit** (`ramp.mjs`, CLI `--mode ramp`) steps the rate up and reports the last loss-free step; `calibrate` checks the board's counters against a known packet count. (iperf itself is not replaced) | node `control.test`, `stream.test` (finds the limit of a simulated forwarder); the real limit: board |
| Pick by the numbers: lower the bitrate if close to the limit | adaptive mode | node |
| A pair of PC tools following that header | `server/stream/cli.mjs` `send` / `recv` (also `--source stdin`) | node `cli.test` (as separate processes) |
| In the dashboard: rate, loss, reordering, jitter next to the board's e0 / w1 curves, so you see which segment loses | `D133Stream.tsx` (path: PC1 → e0 → w1 → PC2 with the loss per segment), `locate.mjs`, `engine.mjs`; interface cards and table in `D133Board.tsx` | node `control.test`, `engine.test`, `stream.test` (loss injected in the cable / inside the D133 / WiFi is found in that segment); screenshot of the page in the simulation |
| "Replace the Pull area" | the Pull form moved to a **Batch** section at the bottom (it is the batch plane); it was not deleted | — |
| Option: take the D133 out of the path | the tools only need the receiver's address: `send --to <PC2>:5600` works with no board | node `cli.test` runs sender and receiver directly |

## What only the board can confirm

`cli.mjs calibrate` sends an exact number of packets through the board and compares the board's counters
before and after. It checks: packets arrive at e0 (`rx_pkts + rx_drop` = sent), the byte count per packet
(is the 14 byte Ethernet header counted? the answer decides `D133_L2_BYTES`), packets leave w1,
nothing is refused at a gentle rate, both interfaces frame alike, and `ip_forwarded` counts them.
Run it first after flashing; anything that is not PASS names what to look at.

The SDK source confirms the premise of the refusal counter: both the Ethernet port (`ethernetif.c`) and the
WiFi port (`wlan_lwip.c`) call `netif->input` and free the pbuf on an error, and lwIP's `tcpip_inpkt`
returns `ERR_MEM` when the mailbox or the message pool is full without counting it. That the hook itself
does not disturb the network on e0 and w1 has not been run on hardware.

## Limits to know

* **Noise floor.** A segment is flagged only when the loss is above 20 packets/s **and** 2 % of what entered:
  ARP and the dashboard's own polling add a few packets. At 270 packets/s a 2 % cable loss and a 3 % WiFi loss
  are below that and show as "ok" (in the simulation only the 8 % loss inside the D133 was flagged).
* **Timing.** The path compares the last 5 s of the sender's, the board's and the receiver's numbers; they are
  sampled once a second on three clocks, so changes show with about a second of misalignment.
* **Jitter** is RFC 3550 on datagrams that arrive in sequence-number order; a datagram overtaken by a newer one
  is counted as reordered and does not enter it, so strong reordering makes jitter read lower.
* **Round trip** is measured on PC1's clock alone (echoed send time minus the receiver's holding time); no
  clock sync is needed, one-way delay is not available.
* **Mbit/s** are application bits per second; the link carries about 5 % more.
* **Windows timers.** The sender paces with Node timers; if the OS timer is coarse the fragments leave in
  small bursts instead of evenly.
* **Counters on the board** are 32 bit; the dashboard takes differences modulo 2^32. lwIP's own counters were
  16 bit by default (they would wrap every few seconds at stream rates, and made the "IP forwarding" rate
  wrong once RT_LWIP_STATS is on); `lwipopts.h` now sets `LWIP_STATS_LARGE`.
