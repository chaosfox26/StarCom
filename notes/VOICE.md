# Voice calls: design

Status: design draft. No code yet.

This covers one-to-one voice over an existing StarCom session (see IDENTITY-AND-DISCOVERY.md for how the session is set up). Group voice comes later.

## 1. Requirements (from SPEC.md and RULES.md)
- **Codec:** Opus, 6 to 510 kbps (spec section 4).
- **Priority:** voice packets travel on the Voice lane, which outranks everything else (spec section 2).
- **Adaptation:** if latency, jitter or loss rise, bitrate drops automatically, using plain feedback rules with no AI (spec section 2).
- **Encryption:** end to end, reusing the session keys; relays see only encrypted packets (rule six).
- **Good neighbour (rule seven):**
  - low CPU;
  - silence costs almost nothing;
  - never more bandwidth than needed.
- **Transparency (rule five):** Opus is listed in COMPONENTS.md, and every packet type is listed in NETWORK.md and DATA.md.

## 2. Components
- **Opus 1.5.2**, the official release from xiph.org. See COMPONENTS.md for the tarball sha256.
  - BSD 3-clause licence, with royalty-free patent licences from its contributors.
  - Vendored in third_party/opus. The `dnn/` folder is EXCLUDED: no neural concealment, redundancy or enhancement, because the spec says no AI.
  - Portable C, with no SIMD or runtime CPU detection, so both operating systems run identical code.
  - Built from the release's own source lists (third_party/opus-sources.txt, 137 files) by build.sh and build.bat.
- **Audio input and output use the operating system's own APIs.** No extra library:
  - Windows: WASAPI (shared mode, event-driven).
  - Linux: PulseAudio or PipeWire through the PulseAudio simple API, loaded at runtime with dlopen (the same pattern as VA-API), so the program still runs without it. ALSA is the fallback.
  - macOS: CoreAudio, later.

## 3. Audio path
    mic -> 48 kHz mono, 20 ms frames (960 samples) -> Opus encode -> seal -> Voice lane -> network
    network -> open -> jitter buffer -> Opus decode (or loss concealment) -> speaker

- **Frame size: 20 ms.** This is the usual trade-off: 10 ms would double the packet rate and header overhead, and 40 ms or more adds delay you can hear.
- **Opus settings:**
  - application VOIP;
  - signal VOICE;
  - variable bitrate, constrained;
  - in-band FEC on (a low-bitrate copy of the previous frame rides in each packet, so one lost packet can be rebuilt);
  - DTX on, so near silence sends one tiny frame every 400 ms instead of 50 frames a second.
- **Complexity:** 5 by default, which is light on CPU. Raised to 8 or more only on a fast machine with good headroom.
- **Mono only for voice.** Stereo and music mode are for streaming, later.

## 4. Packet format (inside the existing Data packet, Voice lane)
Plaintext inside the AEAD:

    kind(1) = VOICE | seq(4) | timestamp(4, in 48 kHz samples) | flags(1) | opus payload

- **seq** detects loss and reordering.
- **timestamp** drives playout and handles DTX gaps.
- **Overhead per packet:**
  - UDP/IP 28 bytes;
  - StarCom header and ids 24 bytes;
  - AEAD tag 16 bytes;
  - voice header 10 bytes;
  - total 78 bytes.
- **At 24 kbps** that is 60 bytes of audio plus 78 bytes of overhead, 50 times a second: about 55 kbps on the wire.
- **To cut overhead later:** 40 ms frames on poor links (the adaptation can switch), and a shorter voice header.
- **Voice is not retransmitted.** A late packet is useless; FEC and concealment cover losses instead. The reliable chat channel is separate and unaffected.

## 5. Jitter buffer
- Adaptive target delay: starts at 40 ms, stays between 20 and 200 ms, and follows the measured jitter (the 95th-percentile inter-arrival deviation over the last 2 seconds, plus a 10 ms margin).
- **A missing frame at playout time:**
  - If the next packet has arrived, decode that packet's FEC data to rebuild the missing frame.
  - Otherwise use Opus loss concealment (decode with no data).
- **Growing and shrinking** is done in silence or DTX gaps, never mid-word. If the buffer grows beyond its target, frames are dropped a little early.
- A packet that arrives after its playout time is dropped.

## 6. Adaptation (the "feedback loop" in the spec)
Measurement and adaptation belong to the **engine** (ENGINE.md), which the spec makes responsible for watching RTT and jitter and stepping bitrates down. There is one feedback loop for the whole session, not one per feature.
- **Voice asks the engine** for its current target (`voiceTargetKbps()`) and a frame-size hint (20 or 40 ms).
- **The engine applies this policy to the voice lane:**

| Condition (from the engine's measurements) | Voice target |
|---|---|
| loss < 2% and RTT < 150 ms and jitter < 30 ms | raise by 10% every 5 s, up to the cap |
| loss 2-10% or jitter 30-80 ms | hold; the FEC loss hint follows the measured loss |
| loss > 10% or RTT > 400 ms or jitter > 80 ms | halve now, minimum 8 kbps; 40 ms frames if still bad after 5 s |

- **Caps:** default 32 kbps; user setting up to 64 kbps for voice; 510 kbps is reserved for music mode later.

## 7. Call flow (voice control on the engine's channel 3, Signal lane)
- Messages: RING, ACCEPT and HANGUP, each carrying a 64-bit call id.
- Calling sends RING every second until an ACCEPT or a HANGUP arrives, or 45 seconds pass ("No answer").
- The called side sees "Incoming call" (window: Accept / Decline; command line: /accept or /hangup). **The microphone opens only after the call is accepted, on both sides.**
- ACCEPT answers every RING of the same call (repeated if needed); HANGUP is sent three times.
- If both press call at once, the side with the lower key keeps its call id and the other side accepts it.
- "Call lost" after 15 seconds without anything from your friend.
- Chat keeps working during a call, on the same session.
- Mute sends silence; Deafen silences what you hear. Both are local switches.

## 8. Who sees what (for NETWORK.md and DATA.md)
- **Audio** is encrypted end to end with the session keys, and **never stored** on disk, by either side or any relay.
- Voice travels through the relays like everything else: your friend never receives your IP address. Each relay sees packet sizes and timing (which rise and fall with speech with variable bitrate) but never the content. A constant-size-packet option is still planned (off by default).
- **The microphone** is open only between accepting a call and hanging up. The app shows this.

## 9. Test plan
- Encode and decode loop on synthetic tones and noise (no real recordings; rule one).
- Random loss of 0/5/10/20% and random reordering through the chaos proxy used for chat. Measure concealment and FEC use, and check that the jitter buffer holds no more than 200 ms.
- Adaptation: step loss from 0 to 15% and back. The bitrate must halve within 1 second and recover within 30 seconds.
- CPU per call on both machines, with a target of under 2% of one core at complexity 5.
- ASan and UBSan on the whole voice core, and the packet parser fuzzed.

## 10. Split
- **Engine** (ENGINE.md): measurement, reports, adaptation, lanes, shaping.
- **Voice core:**
  - Opus wrapper;
  - framing and packet format (section 4);
  - jitter buffer (section 5);
  - FEC and concealment;
  - WASAPI;
  - `call` command.
- **Linux audio** (PulseAudio or ALSA) after the engine.
