# Modular ephemeris: protocol version 4, source plugins, one way to choose a source

This document is the design record for three linked changes. First, a
**source-neutral wire protocol, version 4**, which both `astrolog-ephd` and
Ephemeris Prometheia's `prometheiad` speak natively. Second, **every ephemeris
source as a compiled-in plugin**, used through a fallback chain. Third, **one
way to select a source** from the command line, the settings file and an
Ephemeris Settings dialog in both GUI builds.

Section 3 is **normative**: it is the protocol specification. Once
implemented, `ephsrv/ephproto.h` is the byte-level authority, as it is for
version 3, and this section is the design authority behind it.

## Status — how to pick this back up

- **Where.** Branch `ephv4`, branched from `qt` at d9642c0, in worktree
  `/nvm/work/ephv4`. Each pass lands as one commit on `ephv4`. The whole branch
  is squashed into `qt` when the maintainer says so; never before.
- **Approved plan.** `/nvmraid/home/n/.claude/plans/reactive-percolating-prism.md`
  (2026-09-17). This document supersedes it wherever they differ.
- **Phases.** See §7. The work log (§8) says which are done. Phase 2 is done,
  and work log item 3 cleared what it deferred except **segments** (no fitter;
  the capability is not advertised) and **orbital elements** (kind 4 is a
  per-object error 2 until the fork has an entry point). `cancel` IS
  advertised now: requests are computed in blocks across loop turns.
- **Prometheia.** `/shares/ephemeris-prometheia` pins `ephproto.h`,
  `ephsrv/registries.json` and the conformance fixtures (§3.10). With version
  4 it deletes its wire map (`server/wire_map.*`). Their session is
  `ephemeris-prometheia-0b`. Their last reader run passed **85/85** on the
  previous fixture set with the checksum verified independently.

### The drop is encoded, gated and sent. The reader verdict is the lock.

Section 3 was settled with the Prometheia maintainers over many rounds
(work log items 0, 0b, 0c). Everything agreed is written down: the prose is in
§3.5a, the reasoning and measurements in §8. The one drop that changes bytes
has been built, verified and sent to them as one set (work log item 4). What
remains is their independent reader's verdict on that set -- the gate. Green
locks §3; one disagreement means nobody locks and §3 changes until only one
reading survives. The set, as committed:

1. **`u8 corrApplied` in DATA's META**, immediately after `metaFlags`.
   `resolvedNaif` and `firstFailedRow` shift by one. Its meaning is
   **structural availability** -- the corrections this engine *can* apply to
   this object, for this kind and this observer. A bit is clear when the
   engine cannot apply that term here at all, and stays set when the correction
   ran and contributed nothing. Three bits used (`kCorrLightTime`,
   `kCorrDeflection`, `kCorrAberration`, already in `ephproto.h`), five spare;
   unknown high bits are reserved and clients MUST ignore them. It is
   **diagnostic**: a conformance harness MUST NOT gate comparisons on it
   (work log 0c, the retraction). Normative wording in §3.4 (the META table).
2. **The §3.4 contradiction resolved as drafted** (work log item 2): the
   early-flush suggestion is withdrawn; a server MUST NOT flush each block as
   it completes, because META's `rowsOk`, `firstFailedRow` and `partial` are
   facts about the whole answer. Put to Prometheia as an explicit question in
   the drop; theirs to confirm or counter.
3. **Conformance fixtures regenerated** (`tools/ephproto4-fixtures.py`) with
   the new fixed part, and one new `# set-sha256` in
   `ephsrv/conformance/MANIFEST.tsv`
   (`1c934c7da19965f21ded99a0a53e36eaa3c45434cfbfaeabddafaf154ea454ec`,
   recomputed independently from the on-disk bytes).
4. **The set sent whole** to Prometheia (`ephemeris-prometheia-0b`):
   `ephsrv/ephproto.h`, `ephsrv/registries.json` and the fixtures with their
   digest, plus the §3.4 question and the corrApplied wording question. Their
   reader's verdicts on THAT set are the gate; their header
   leniency/strictness review rides in the same reply.

**Then stop and ask the maintainer about phases 3-7.** They were never
approved as automatically next; §7 lists them and they are a separate
decision.

- **Open for the maintainer, no action needed before the lock.** Whether to
  patch vendored Swiss's `lunar_osc_elem()` to retard in the barycentric
  frame. Work log 0c has the argument; the recommendation on this branch is
  **no**, and Prometheia withdrew the suggestion when given it.
- **How to run a round with them** is `EPHEMERIS_PROTOCOL_COLLABORATION.md`:
  what a drop contains, why it is sent whole, and the discipline that made the
  objections on both sides productive. Read it before sending anything.
- **A narrative handoff** with the round-by-round reasoning lives at
  `/nvm/work/ephv4-handoff.md`. It is scratch and not in git; everything
  load-bearing from it is in this document, which is the authority.

## 1. Why

These were found on 2026-09-17 while pointing the Qt GUI at a local server.
Every item below was verified on branch `qt` at d9642c0.

**Selection state and switches**

1. **One choice is spread over three fields.** `us.fEphemFiles`,
   `us.nSwissEph` and `us.fMatrixPla` (astrolog.h ~2230–2334) together hold the
   selected source. Many combinations mean nothing. "None" is not stored at all:
   it is simply files off with Matrix off.
2. **Two numberings for the same thing.** The `cm*` enum (astrolog.h:1295) and
   `nSwissEph` number sources differently: Horizons is 4 in one and 3 in the
   other, the server 6 and 5. This caused bug C15.
   - `FCmJPLWeb` is `nSwissEph >= 3` (extern.h:152), so it is also true for the
     server.
   - Win32's Calculation Settings shows a server selection as Horizons, and OK
     saves it as Horizons.
3. **The command-line toggles depend on prior state** (switch.cpp:2269–2383).
   - Every `-b` suffix also toggles `fEphemFiles`. From the defaults, `-bj`,
     `-bs`, `-bJ` or `-bU` therefore gives None.
   - `-bS` turns files on, but a second `-bS` turns them off.
   - `_bX` clears any backend, not only its own.
4. **The settings file only round-trips because of line order.** The writer
   emits lines in a fixed order that loading depends on (io.cpp:1795–1830).
5. **Upstream's one-way locks are shipped on.** `=0b` and `=0n` sit in
   `astrolog.as` and cannot be undone (`_0` does nothing, switch.cpp:3849).
   - Matrix, Horizons and the server cannot be chosen anywhere.
   - The dialogs hide them.
   - A saved file that selects one of them fails to load.

**Connection**

6. **A startup nag and slow retries.** A modal "Connecting to cloud ephemeris"
   dialog appears at startup, and closing it exits with code 86. Before any
   session, retries run on a 60 s tick.

**Hidden dependence on local Swiss files**

7. **Side calculations assume local Swiss files.** Progressed arcs, the eclipse
   Sun, fixed stars, planet phenomena and asteroid listings all do, whatever
   source is selected.

**Protocol**

8. **Version 3 is Swiss-numbered.** Bodies, flag bits and sidereal modes use the
   Swiss Ephemeris numbering, so any other engine needs a hand-written wire map.
   Prometheia's `server/wire_map.hpp` exists only for that reason.
9. **Version 3 cannot express one cast.**
   - The observer and flags vary per object within a single cast (fMoonMove,
     a custom object's nFlg, geocentric nodes in a heliocentric chart, the
     barycentric Sun), so the client splits a cast into several requests
     (qtdriver.cpp ~8530).
   - Sidereal mode "Fagan-Bradley on the invariable plane" is mode 0 plus a
     plane bit (calc.cpp:3714).
10. **Small items.**
    - The AstroExpression `_b` returns `us.fEphemeris` (the -E flag), not the
      ephemeris-files setting.
    - `is.fNoEphFile` is a single warning latch shared by unrelated failures.
    - There is no setting for the JPL file name.

## 2. Decisions (maintainer, 2026-09-17)

**Sources**
- **No switch disables network sources.** A selected network source is used
  if it is reachable, and a failure is reported when it happens.
- **Plugins are compiled in only.**
- **What a plugin supplies.** Body positions, with capabilities it declares.
  Houses, ΔT, the calendar and refraction are shared formulas; they need no data
  files.
- **Sources are not tied to Swiss.** The design assumes only the selected
  ephemeris is available: **every call tries the primary source, then
  successively less ideal ones**, and records which source answered.
- **Horizons is rewritten** as a proper plugin that takes instants.

**Builds**
- **Full Win32 parity**, including the Ephemeris Settings dialog.
- **Real server transports in every build.** Qt uses QWebSocket, Win32 uses
  WinHTTP's WebSocket client, and the console build uses a plain socket
  client.

**Protocol version 4**
- **Bodies are named by NAIF/SPK-IDs**, with typed extras for what NAIF does
  not cover.
- **Options are explicit typed fields.**
- **Clean break.** kProtoMin = 4. No released client speaks 2 or 3 (qt.24
  shipped version 1), and no public server exists.
- **Discovery and provenance.** Capabilities come in WELCOME, provenance per
  object in DATA, and there is a LOOKUP message.
- **Segments are normative but gated by a capability.**

**Settled in design review**
- **Local Swiss stays bit-exact** through a host-only native-id hint.
- **The Swiss fork gains an orbital-elements entry point.**

## 3. Protocol version 4 (normative)

The key words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

### 3.1 Transport and conventions

**Transport**
- **WebSocket, binary frames only.** Each frame carries exactly one message.
  A text frame is ERROR 1.
- **Default port 47190.** The server port also answers `GET /healthz`,
  `/readyz` and `/metrics`.
- **TLS.** `wss://`, with TLS 1.2 or later.

**Encoding**
- **Byte order.** Integers are little-endian. `f32` and `f64` are IEEE-754 bit
  patterns, little-endian.
- **`str8`.** A u8 length followed by that many bytes of UTF-8, with no
  terminator. The bytes MUST be valid UTF-8 with no C0 control characters.
- **`TIME`.** Two f64 values, `jd1` and `jd2`: a two-part Julian date whose
  instant is jd1 + jd2. A sender that has a single double sends it as jd1 with
  jd2 = 0. Every TIME MUST be finite, with |jd1| + |jd2| ≤ 1e8.
- **TLV area.** A u16 `totalLen` counts the entry bytes that follow it. Each
  entry is `{u16 tag, u16 len, len bytes}`.
  - Tags MUST be strictly ascending by their full u16 value, with no duplicates.
  - Bit 15 of a tag is **critical**. A receiver that does not know a critical
    tag MUST refuse the message: for REQUEST that is ERROR 11, closing = 0.
    Unknown non-critical tags MUST be ignored, and a server that ignored one sets
    `ignoredExt` in its answer.
  - Tags 0x0001–0x6FFF (with or without bit 15) are assigned in Appendix A.
    0x7000–0x7FFF (with or without bit 15) are experimental and never assigned.
  - Tag numbers are scoped to the message type whose TLV area they appear in.
- **Reserved fields** MUST be zero. So MUST fields a message's other choices
  make unused (§3.5).
- **Enumerated fields** carry registry values (Appendix A). A value the
  receiver does not implement makes REQUEST fail with ERROR 11, not ERROR 1:
  registries grow.
- **The answering direction tolerates growth.** Registries grow while released
  clients stay in use, so a client MUST accept a server message carrying a value
  it does not know, and MUST NOT treat it as malformed:
  - an unknown per-object `errCode` means that object failed, reason unknown;
  - unknown META flag bits are ignored;
  - an unknown ERROR code is a failure of unknown kind — `flags` still says
    whether the connection is closing and whether a retry may work;
  - an unknown match `quality`, orbit method or object kind inside a
    LOOKUP_RESULT means "not something this client can ask for": the client
    skips that match using its `matchLen` and reads the rest;
  - unknown bits in an answer's flag fields — ERROR `flags`, DATA and SEGDATA
    `chunkFlags`, LOOKUP_RESULT `flags` — are ignored, and a bit added to those
    fields later MUST NOT change how following bytes are read, precisely so
    that this stays possible.
  The exception is anything that does change how following bytes are read: an
  unadvertised bit in DATA's `columnsPresent` changes the row width, so it is
  malformed.
- **Floats** MUST be finite. The single exception is the canonical quiet NaN,
  `0x7FF8000000000000`, which is allowed only where a field says so.
- **Canonical input.** A receiver MUST reject non-canonical input (ERROR 1)
  rather than normalise it. This makes byte equality mean question equality,
  and the result cache depends on it (§3.7).

### 3.2 Envelope

Every message is a 16-byte envelope followed by `payloadLen` bytes of payload.

| offset | type | field |
|---|---|---|
| 0 | u16 | magic `0x1EF0` |
| 2 | u8 | version the message is written in |
| 3 | u8 | flags: bit 0 `zstd` (payload compressed with zstd; only when both ends advertised it); bits 1–7 zero |
| 4 | u16 | type (Appendix A.1) |
| 6 | u16 | reserved, zero (held for channel multiplexing) |
| 8 | u32 | requestId |
| 12 | u32 | payloadLen |

- **payloadLen** MUST NOT exceed WELCOME's `maxPayload` after the session is
  established. Before that the limit is 64 KiB.
- **requestId.**
  - The client chooses a request's id. It MUST be nonzero and MUST NOT be reused
    while that request's answer is outstanding.
  - Answers carry the id of the question they answer.
  - It MUST be 0 on HELLO, WELCOME, PING, PONG and a connection-level ERROR, and
    MUST be nonzero on REQUEST, CANCEL and LOOKUP and on the DATA, SEGDATA,
    LOOKUP_RESULT and ERROR messages answering them.
- **Unknown message type.** ERROR 3, not closing.

### 3.3 Negotiation and frozen layouts

The layouts of **HELLO, WELCOME's first 28 bytes, ERROR, PING, PONG and
CANCEL** are frozen for every version ≥ 4. Later versions extend them only
through their TLV areas. This is what lets two ends of any versions talk long
enough to agree or refuse.

1. **HELLO.** The client sends HELLO first. Its envelope version is the
   client's highest version.
2. **Session version.** A server MUST accept a HELLO in any envelope version
   ≥ 4. It computes `session = min(client.protoMax, server.max)`.
3. **No overlap.** If `session < max(client.protoMin, server.min)`, the server
   sends ERROR 8 (closing) in version `session` if that is ≥ 4, otherwise in
   version 4, and closes.
4. **Otherwise** the server answers WELCOME in `session`. Every later message
   in either direction is written in `session`.
5. **Older clients.** An envelope with version < 4 comes from an older client.
   The server answers ERROR 8 in **that client's own layout** (versions 2–3:
   `u32 requestId, i32 code, NUL-terminated text`) in that version, and closes.
6. **Message before HELLO.** Any message other than HELLO and PING before HELLO
   gets ERROR 1 (closing).
7. **Repeated HELLO.** A second HELLO is answered with WELCOME in the
   established session, which is not renegotiated.
8. **Missing HELLO.** A server MAY close a connection that sends no HELLO within
   its deadline.

### 3.4 Messages

Payload layouts follow; `…` marks a variable part.

#### HELLO (1, client → server)
| type | field |
|---|---|
| u32 | protoMax — the client's highest version |
| u32 | protoMin — the client's lowest version (4) |
| u32 | build — the client's build number, or 0 |
| u32 | clientCaps — Appendix A.2 bits the client can use |
| str8 | clientName — e.g. `Astrolog 8.00-qt.25` |
| str8 | token — empty for none; at most 128 bytes; never logged by servers |
| TLV | HELLO extensions (none assigned) |

#### WELCOME (2, server → client)
| type | field |
|---|---|
| u32 | protoSession |
| u32 | caps — Appendix A.2 |
| u32 | maxObjs — objects per REQUEST |
| u32 | maxRows — rows (instants) per REQUEST |
| u32 | maxChunkRows — rows per DATA chunk |
| u32 | maxPayload — bytes per message |
| u32 | maxCells — objects × rows per REQUEST |
| u8 | maxProfiles |
| u8, u16 | reserved |
| str8 | serverName — e.g. `astrolog-ephd/2.0`, `prometheiad/0.2` |
| str8 | engine — human-readable, e.g. `Swiss Ephemeris 2.10.03 files`, `Prometheia 0.2, JPL DE440 + SBDB 2026-09-16` |
| str8 | datasetId — `<engine>/<ephemeris>/<catalogs>#<8 hex>`, the digest over every data file's checksum and the engine version. It MUST change whenever any answer the server gives could change. Clients key caches on it, and may pin it (A.4 0x8003). |
| TLV | capabilities (Appendix A.3). Tags 0x0001–0x0006, 0x0008 and 0x0009 MUST be present. |

A client MUST NOT send an option value, object kind, column or message that
the server did not advertise. A server MUST refuse one that it does not
support, with ERROR 11 or a per-object error as §3.5 says.

#### REQUEST (3, client → server)
The payload is a **delivery block**, then a **question block**. Only the
question block goes into the cache key.

Delivery block (12 bytes):
| type | field |
|---|---|
| u8 | precision — 0 f64, 1 f32 (DATA values only) |
| u8 | priority — 0 interactive, 1 prefetch |
| u8 | representation — 0 samples (answered with DATA), 1 segments (answered with SEGDATA; requires the `segments` cap) |
| u8 | maxDegreeHint — segments only: the largest Chebyshev degree the client wants to buffer; 0 lets the server choose. The server also caps by its own capability (A.3 0x000F). 0 for samples |
| u32 | chunkRows — a hint; the server clamps it to `[1, maxChunkRows]`, and 0 means `maxChunkRows` |
| f32 | segTargetErrArcsec — 0 for samples; for segments a finite value > 0, the angular error the client asks for |
| u32 | deadlineMs — how soon the client wants the answer, 0 for "no deadline stated". Advisory: a server MAY choose a cheaper strategy to meet it (samples now rather than a fit), and MUST NOT fail a request for missing it. `priority` says which order to work in; this says how long the work may take, which is a different question and the one that decides between two strategies |

Question block:
| type | field |
|---|---|
| u8 | timeScale — Appendix A.9 (0 UT1, 1 TT, 2 TDB) |
| u8 | timeMode — 0 grid, 1 list |
| u16 | reserved |
| … | grid: `TIME start`, `i64 stepNs`, `u32 nTime` · list: `u32 nTime`, `nTime × TIME` |
| f64 | deltaTSec — TT − UT1 in seconds, or the canonical NaN for "the server's model" |
| u8 | nProfiles (1..maxProfiles) |
| … | nProfiles × PROFILE |
| u16 | nObj (1..maxObjs) |
| … | nObj × OBJECT |
| TLV | REQUEST extensions (Appendix A.4) |

PROFILE:
| type | field |
|---|---|
| u8 | observer — A.5 (0 geocentric, 1 topocentric, 2 heliocentric, 3 solar-system barycentre, 4 body) |
| u8 | plane — A.6 (0 ecliptic, 1 equator) |
| u8 | form — A.6 (0 spherical, 1 rectangular) |
| u8 | frame — A.6 (0 true of date, 1 mean of date, 2 J2000, 3 ICRF) |
| u8 | corrections — A.7 bits (light time 1, deflection 2, aberration 4) |
| u8 | speeds — 0 or 1 |
| u8 | siderealPlane — A.8 (0 ecliptic of date, 1 ecliptic of the anchor epoch, 2 invariable plane of the solar system) |
| u8 | reserved |
| i32 | observerBody — NAIF id when observer = 4, else 0 |
| f64 ×3 | siteLonEastDeg, siteLatDeg, siteHeightM — when observer = 1, else 0 |
| TIME | anchorEpoch — for zodiac `user`, else zero |
| f64 | anchorAyanamsaDeg — for zodiac `user`, else 0 |
| u32 | columns — A.10 extra columns wanted |
| str8 | zodiac — "" tropical, otherwise a token from A.11 |
| TLV | PROFILE extensions (none assigned) |

OBJECT (a 4-byte head, then a payload that depends on the kind; Appendix A.12):
| type | field |
|---|---|
| u8 | kind |
| u8 | profile — an index < nProfiles |
| u16 | reserved |

| kind | payload |
|---|---|
| 0 body | `i32 naif` |
| 1 orbit point | `i32 naif`, `u8 point` (A.13), `u8 method` (A.14), `u16 reserved` |
| 2 fixed star | `str8 name` — an IAU proper name, Bayer, Flamsteed, HR, HD or HIP designation (§3.5a) |
| 3 named hypothetical | `str8 name` — a token from A.15; elements server-defined (§3.5a) |
| 4 elements | `TIME epoch`, `u8 equinox` (A.16), `u8 centre` (0 Sun, 1 Earth), `u8 nTerms` (1..5), `u8 reserved`, `f64 equinoxJd` (A.16 value 4 only, else 0), then `6 × nTerms f64`: the polynomial coefficients c0..c(nTerms−1) of, in order, mean anomaly M (deg), semi-major axis a (AU), eccentricity e, argument of perihelion ω (deg), ascending node Ω (deg), inclination i (deg); then `str8 name` |
| 5 designation | `str8 designation` — resolved as an exact LOOKUP; ambiguity is a per-object error (§3.5) |

#### DATA (4, server → client)
Answers a samples REQUEST in one or more chunks. Header (24 bytes):
| type | field |
|---|---|
| u32 | chunkIndex — 0, 1, … |
| u32 | iTime — index of this chunk's first row |
| u32 | nRows — rows in this chunk |
| u32 | totalRows — the request's nTime |
| u8 | precision — as requested |
| u8 | chunkFlags — bit 0 last chunk, bit 1 ignoredExt, bit 2 meta present |
| u16 | nObj |
| u32 | columnsPresent — the extra columns in this answer (§3.5) |

**Metadata.** If the meta-present flag is set, the source table and the object
metadata follow the header:
- **Source table.** `u8 nSources`, then `nSources × str8`: the sources that
  answered, e.g. `JPL DE440`, `Swiss Ephemeris files (sepl_18)`, `SBDB 2026-09-16`.
- **Object metadata.** `nObj × META`.

The meta-present flag MUST be set on chunk 0. A server MAY repeat the metadata
on later chunks, and clients use chunk 0's.

META:
| type | field |
|---|---|
| i32 | rowsOk — rows of this object that computed |
| u16 | errCode — A.17; the first failure's code, else 0 |
| u8 | sourceIdx — index into the source table, 0xFF if none |
| u8 | metaFlags — A.18 |
| u8 | corrApplied — A.7 bits; see below |
| i32 | resolvedNaif — the NAIF id actually computed, or INT32_MIN when not applicable |
| u32 | firstFailedRow — 0xFFFFFFFF if none |
| str8 | name — display name, e.g. `Ceres`, `Moon mean apogee` |
| str8 | errText — the first failure's text, else empty |

`corrApplied` says which correction terms are live for this object here --
**structural availability**, not the request's mask. A bit is clear when the
engine cannot apply that term to this object, for this kind and this
observer, at all; it stays set when the term's model ran and contributed
nothing (a deflection that returns zero far from the Sun has still been
applied). Three bits from A.7 (`kCorrLightTime`, `kCorrDeflection`,
`kCorrAberration`); the five low spares are zero, and unknown high bits are
reserved: clients MUST ignore them and servers MUST NOT refuse them. The
field is **diagnostic only**: it explains a difference; it does not predict
one, and equality of it is neither necessary nor sufficient for two answers
to agree. A conformance harness MUST NOT gate comparisons on it.

**Values.** Objects in request order. For each object come the chunk's rows in
order, and each row holds `nCols = 6 + popcount(columnsPresent)` values (f64 or
f32 as the precision says). The column order is in §3.5.

**Chunks.** Chunks are contiguous and ascending, covering every row exactly
once.

#### ERROR (5, server → client)
| type | field |
|---|---|
| u16 | code — A.19 |
| u16 | flags — bit 0 closing (the server closes after sending it), bit 1 retryable |
| u32 | retryAfterMs — 0 if unknown |
| str8 | text — human-readable; MUST NOT contain request contents that identify a person's chart (instants, places) |
| TLV | ERROR extensions (none assigned) |

The envelope's requestId names the failed request, or 0 for the connection.

#### PING (6) / PONG (7)
The payload is empty. Either end MAY send PING at any time, and the other end
answers PONG with the same requestId.

#### CANCEL (8, client → server)
The payload is empty, and the envelope's requestId names the request.
- **Still being answered.** The server stops computing what is left, discards
  the unsent chunks and sends ERROR 10 (not closing, not retryable).
- **A cancelled request caches nothing.** A partial answer MUST NOT become a
  cache entry, or a later identical question hits half an answer.
- **To make CANCEL mean anything**, a server computes a request in blocks of
  rows across loop turns rather than in one callback: otherwise the work is
  finished by the time the CANCEL is read and only bytes remain to drop, and a
  large request blocks that loop for everyone (64 objects × 20,000 rows measured
  13.7 s, EPHEMERIS_REVIEW.md S4). A server MUST NOT flush each block as it
  completes: DATA carries the metadata on chunk 0, and META's `rowsOk`,
  `firstFailedRow` and `partial` are facts about the whole answer, which an
  early flush would have to write before they are known. A server that
  computes in blocks therefore holds the finished ones and streams when the
  answer is whole.
- **Already answered completely, or unknown.** The server sends nothing.
- **Chunks already in flight.** The client MUST ignore chunks that arrive for a
  cancelled id.

#### LOOKUP (9, client → server)
Requires the `lookup` cap.
| type | field |
|---|---|
| u16 | maxMatches — the budget for the WHOLE message, 1..the lookup TLV's limit |
| u8 | flags — bit 0 prefix match, bit 1 include hypotheticals, bit 2 include stars |
| u8 | nQueries — 1..255 |
| … | nQueries × `str8 query` — a name, designation or number, case-insensitive |
| TLV | LOOKUP extensions (none assigned) |

`maxMatches` bounds the answer as a whole, not each query: the server fills it
in query order and sets `truncated` when it runs out. A per-query budget would
make one small message ask for 255 × 65,535 matches, and a prefix query like
"a" matches thousands of stars in a real catalogue. Batching is what makes
LOOKUP usable at all for a body picker: Astrolog's Object Selections offers 78
bodies, which is 78 round trips one query at a time.

#### LOOKUP_RESULT (10, server → client)
| type | field |
|---|---|
| u8 | nQueries — the same count the LOOKUP carried, in the same order |
| u8 | flags — bit 0 truncated (the message budget ran out) |
| u8 | nSources, then nSources × str8 — shared by every query |
| … | per query: `u16 n`, then n × MATCH |

MATCH:
| type | field |
|---|---|
| u8 | quality — 0 exact canonical name, 1 exact alias or designation, 2 prefix |
| u8 | sourceIdx |
| u16 | matchLen — bytes from the end of this field to the end of this MATCH, so a client can skip a match whose object kind it does not know (§3.1). A value that disagrees with a match the client CAN read is malformed |
| … | OBJECT (kind + profile 0 + reserved + payload) — what to put in a REQUEST |
| str8 | canonicalName |
| str8 | designation — e.g. `2060`, `1P/Halley`, empty if none |
| TIME, TIME | validMin, validMax — the coverage for this object; both zero when unknown |

Matches are ordered by quality, then by the server's relevance. A query can
legitimately match several kinds: "Lilith" is asteroid 1181, the Moon's mean
apogee, and a hypothetical body.

#### SEGDATA (15, server → client)
Answers a segments REQUEST (`representation = 1`). Header (16 bytes):
| type | field |
|---|---|
| u32 | chunkIndex |
| u8 | chunkFlags — bit 0 last, bit 1 ignoredExt, bit 2 meta present |
| u8 | reserved |
| u16 | nObj — in the whole answer |
| u16 | iObj — first object in this chunk |
| u16 | nObjChunk |
| u32 | reserved |

Then, when meta is present (always on chunk 0), the source table and
`nObj × META` exactly as in DATA. Here `rowsOk` counts the segments served, and
`firstFailedRow` is unused (0xFFFFFFFF). Chunk 0 then carries the **ayanamsa
series**: `u8 nAyan`, and for each, `u8 profile`, `u32 nSeg`, `nSeg × AYANSEG`
— one entry per profile whose zodiac is not tropical, none otherwise. After
that, for each object from iObj to iObj + nObjChunk − 1: `u32 nSeg` and
`nSeg × SEGMENT`.

AYANSEG: `TIME mid`, `f64 halfSpanDays`, `u8 degree`, `u8 ×3 reserved`,
`f32 errArcsec`, `(degree+1) f64` — the ayanamsa in degrees, evaluated as a
SEGMENT's axis is, covering the same span. A mean ayanamsa needs about degree
3 over a century; a true one needs more.

SEGMENT:
| type | field |
|---|---|
| TIME | mid — the segment's centre |
| f64 | halfSpanDays — h > 0 |
| u8 | degree — d, 0..31 |
| u8 ×3 | reserved |
| f32 | errArcsec — the largest residual the server MEASURED on the direction |
| f32 | errRelDist — the largest measured relative distance residual |
| f32 | errRateArcsecPerDay — the largest measured residual of the analytic derivative against the server's own rates |
| f64 × 3(d+1) | Chebyshev coefficients: x0..xd, y0..yd, z0..zd |

Segment rules:
- **Required form.** A segments REQUEST's profiles MUST have `form = 1`
  (rectangular) and `columns = 0`, or the REQUEST is ERROR 11.
- **Grid.** Grid mode is required; the list mode is ERROR 11. The requested
  **span** runs from the grid's first instant to its last.
- **Coverage.** Segments for an object are ordered by `mid`, and together they
  cover the span contiguously. The end of one, mid + h, equals the start of the
  next, mid′ − h′, to within 1e-9 day.
- **Evaluation.** For an instant t:
  - τ = ((t.jd1 − mid.jd1) + (t.jd2 − mid.jd2)) / h, with |τ| ≤ 1.
  - The position (AU) is x = Σₖ xₖ Tₖ(τ), and likewise y and z, where Tₖ is the
    Chebyshev polynomial of the first kind.
  - The velocity (AU/day) is Σₖ xₖ T′ₖ(τ) / h.
  - The coordinates are rectangular in the profile's observer, plane, frame and
    corrections. For a sidereal zodiac, the rotation about the plane's pole by
    the ayanamsa at each instant is included in the fit.
- **Residuals, measured not claimed.** `errArcsec`, `errRelDist` and
  `errRateArcsecPerDay` are the largest residuals the server MEASURED against
  its own answers over the segment, on a check set of at least 4(d+1) instants
  that **MUST include both endpoints of the segment's interval** (τ = ±1) as
  well as points between the fit nodes. The endpoints are not optional: a
  Chebyshev interpolant's error peaks near the interval ends, and its
  DERIVATIVE's error peaks there much harder. Measured: the Moon's
  `errRateArcsecPerDay` declared from interior points alone was 1.48″/day where
  an independent sample found 3.6″/day; with the endpoints in the check set the
  same fit declares 4.65″/day. A server obeying the weaker reading
  under-reports by a factor of two to three, always in its own favour. The server SHOULD make
  `errArcsec` ≤ `segTargetErrArcsec`; where it cannot, it reports what it
  measured. (A rigorous bound costs more than the fit; a dense measured
  residual is cheap and honest.)
- **The zodiac is not in the fit.** Coefficients are always tropical in the
  profile's frame. When a profile carries a zodiac, the answer also carries
  that profile's **ayanamsa series** (below), and the client subtracts it. One
  fit then serves every zodiac, and a true-of-date ayanamsa -- which carries
  nutation in longitude, 17″ with an 18.6-year period -- is fitted where it
  belongs instead of roughening every body's fit.
- **Fit the lattice, not the ask.** A server SHOULD fit fixed lattice cells
  that cover the requested span -- 32 days, aligned from J2000 -- rather than
  the span it was asked for, so that two clients whose spans overlap share the
  fits. The lattice is the SERVER's: a client MUST NOT be able to name a
  boundary, or the sharing is gone. Measured cost of doing so (one 384-day span
  against twelve 32-day cells, same body, same target): the worst case is +14%
  sampler calls (the Moon at 1″), and in half the cases the cells are CHEAPER,
  because fitting a year as one interval wastes probes before it splits. The
  client is unaffected: it gets contiguous coverage of what it asked for, and a
  little either side.
- **Quantise the ask, downward only.** A server MAY round
  `segTargetErrArcsec` onto its own ladder (0.001 / 0.01 / 0.1 / 1″), so that
  two clients asking 0.1 and 0.12 share one fit, but it MUST round toward a
  FINER fit, never a coarser one. The residual it reports is the truth about
  what was served, but a client asked for a number because something downstream
  depends on it, and "you got worse than you asked, look at the metadata" is a
  defect that surfaces as a wrong ingress time three layers away. Rounding
  finer costs only the server, which is the right party to bear it.
- **Which objects.** The segments capability (A.3 0x000F) carries an A.12
  kinds bitmask; an object of another kind is per-object error 2. Some objects
  fit badly on purpose: the Moon's osculating perigee moves degrees a day and
  its longitude rate changes sign.
- **Failure.** An object that could not be computed has `nSeg = 0` and its
  error in META.

#### Reserved message types
- **11 SUBSCRIBE, 12 UNSUBSCRIBE.** Server-pushed windows that follow an
  anchor instant.
- **13 LIST, 14 LIST_RESULT.** Catalog paging with an opaque cursor.
- **16–31.** Event searches: stations, ingresses, aspects, eclipses, rise and
  set, heliacal phenomena.

These numbers are reserved and have no layout yet.

### 3.5 Semantics

**Instants**
- **Grid.** The instant of row r is the TIME
  `(start.jd1, start.jd2 + off)`, where `off = (double)(r × stepNs) /
  86400000000000.0`.
  - The product `r × stepNs` is computed in i64. A REQUEST where
    `(nTime−1) × |stepNs|` overflows i64 is ERROR 1.
  - An engine that takes one double evaluates `start.jd1 + (start.jd2 + off)`.
  - `stepNs` is signed. It MUST be 0 when nTime = 1 and nonzero otherwise.
- **List.** Instants in any order; duplicates are allowed.
- **Time scale.** Instants are in `timeScale`. ΔT (TT − UT1) converts UT1
  instants and drives Earth rotation for topocentric work. Its value at each
  row comes from, in order:
  1. the **ΔT table** REQUEST TLV (0x8004, A.4), when present: piecewise-linear
     interpolation in the table's instants, held constant beyond its ends;
     `deltaTSec` MUST then be the canonical NaN. **The table's instants are
     TT**, whatever the request's time scale, and a row's ΔT is interpolated at
     that row's own numeric instant with no iteration — deterministic on both
     ends, and for a UT1 row the ΔT so found differs from the TT-argument value
     by under 1e-5 s;
  2. a finite `deltaTSec`: that one value for every row, which is the
     client's responsibility to keep valid over the span it asks (ΔT drifts
     about a second a year today, and far faster historically);
  3. otherwise the server's own ΔT model, named in the ΔT-model capability.

  A client that owns ΔT, as Astrolog does, sends TT instants and a ΔT table
  (or, for short spans, one `deltaTSec`).

**Observers and options**
- **Observer.**
  - Correction bits are **honoured as sent, for every observer** (§3.5a). A
    server advertises, per observer, the masks it can honour (A.3 tag 0x0004);
    any other combination is ERROR 11.
  - A body observer (4) with `observerBody` equal to the object is a per-object
    error 2.
- **Sidereal zodiacs apply to the ecliptic plane only.** A PROFILE with a
  nonempty zodiac and `plane = 1` (equator) is ERROR 1.
- **Unused fields.** `observerBody`, the site, `anchorEpoch` and
  `anchorAyanamsaDeg` MUST be zero unless the observer or zodiac uses them.
  - Site ranges: longitude in [−180, 180], latitude in [−90, 90].
  - Zodiac `""` (tropical) requires `siderealPlane = 0`.
  - Zodiac `user` requires a nonzero anchor epoch.
- **Speeds.** When `speeds = 0`, the three rate columns are 0 and META's
  `noSpeeds` flag is set. When `speeds = 1`, rates are as §3.5a defines.

**Columns**, per row:
- The base six:
  - ecliptic spherical: longitude and latitude (deg), distance (AU), then their
    rates (deg/day, AU/day);
  - equatorial spherical: right ascension and declination (deg), distance, then
    their rates;
  - rectangular: x, y, z (AU), vx, vy, vz (AU/day).
- Longitudes and right ascensions are in [0, 360).
- For a sidereal zodiac, longitudes are reduced as §3.5a defines; with
  rectangular form the vector is rotated about the ecliptic pole by the same
  ayanamsa, so the two forms stay consistent.
- **Distance unknown** (a star without a parallax): the distance column and its
  rate are 0, and META's `noDistance` flag is set.
- Extra columns follow in A.10 bit order, for the bits in `columnsPresent`,
  which is the requested columns intersected with the server's advertised ones.
  - σ is valid only where META's `hasSigma` is set, and is 0 elsewhere.
  - Ayanamsa is the value subtracted for that row, in degrees (§3.5a); 0 for
    tropical.
  - Light time is the τ applied, in days; 0 when light time is off.
  - ΔT is the TT − UT1 used for that row, in seconds.

**Failure**
- **Row failure.** A row that failed has NaN in every column of that row. An
  object keeps its computed rows (`partial`).
- **Object failure.** An object whose rows all failed has `rowsOk = 0` and its
  errCode and errText set. **One object's failure never fails the request.**
- **Whole-request failures** are only those in A.19.

**Limits**
- `nObj ≤ maxObjs`, `nTime ≤ maxRows`, `nObj × nTime ≤ maxCells` and
  `nProfiles ≤ maxProfiles`; otherwise ERROR 2.
- A connection holding a server-chosen number of answers the client has not yet
  read (4 in `astrolog-ephd`) gets ERROR 9 (retryable) for the next one.
- A server enforcing a compute budget answers ERROR 6 (retryable) with
  `retryAfterMs`.

**Ordering and delivery**
- A server MAY interleave chunks of different requests, and MAY answer
  priority 0 before priority 1. Within one request, chunks stay in order.

**Pins** (REQUEST TLVs 0x8001 and 0x8002, both critical)
- The request is answered only from the named ephemeris or catalog snapshot, or
  refused with ERROR 5.
- This gives reproducibility: a chart can record the `datasetId` or pins it was
  cast with.

**Designations** (kind 5) resolve exactly as LOOKUP with quality 0 or 1.
- More than one match is per-object error 6.
- No match is per-object error 1.

### 3.5a Physical definitions (normative)

Two conforming servers given the same question MUST return the same
quantity. These definitions say which quantity; the numbers then differ only
by the ephemerides and models each server names in WELCOME (engine,
datasetId, coverage, precession models, ΔT model) and per object (the source
table).

**Frames and planes** (default precession model IAU 2006; REQUEST TLV 0x0003
may select another from A.20):
- **True of date (0).** Equator: the true equator and true equinox of date
  (IAU 2006 precession, IAU 2000A nutation or the server's advertised
  equivalent). Ecliptic: the mean ecliptic of date, longitudes counted from the
  true equinox of date (nutation in longitude applied, latitude unchanged by it).
- **Mean of date (1).** The mean equator, mean ecliptic and mean equinox of
  date; no nutation.
- **J2000 (2).** The mean equator and equinox of J2000.0, **including frame
  bias** (the IAU 2006 J2000 mean dynamical frame). Its ecliptic is that equator
  rotated by the IAU 2006 J2000 mean obliquity, 84381.406″.
- **ICRF (3).** ICRS axes, no bias. Its ecliptic is the ICRS equator rotated by
  84381.406″ about the ICRS x axis.
- **Topocentric site.** WGS84 ellipsoid; east longitude and geodetic latitude
  in degrees; height above the ellipsoid in metres.

**Corrections**, honoured as sent for every observer:
- **Light time (1):** the body's position at the retarded instant t − τ, τ the
  light time from body to observer.
- **Deflection (2):** gravitational light deflection by the Sun, applied for
  every observer that is not the Sun itself — the barycentre included, which
  sits about 0.005 AU from the Sun's centre. For an observer at the Sun's centre
  the bit has no effect. A direction inside the solar disc, as seen from the
  observer, skips deflection rather than evaluating a formula whose denominator
  vanishes there.
- **Aberration (4):** relativistic aberration from the observer's velocity
  relative to the solar-system barycentre, whatever the observer — the Earth's
  (geocentric), the site's (topocentric, including diurnal motion), the Sun's
  (heliocentric), zero at the barycentre, the observing body's (observer 4).
- A server that cannot honour a mask for an observer does not advertise that
  pair (A.3 0x0004); the client then asks for a mask it can.

**Rates** (`speeds = 1`):
- The rate columns are **the time derivatives, per day of the request's time
  scale, of the coordinates answered in the other three columns** — including
  every change of the pipeline with time (light time, aberration, precession,
  nutation, the ayanamsa for sidereal zodiacs). Rectangular velocities are the
  derivatives of the answered x, y, z likewise.
- A server whose rates may differ from the central difference of its own
  positions over ±0.001 day by more than **1e-5 °/day** (angles) or
  **1e-6 AU/day** (distance) sets META's `ratesApprox` flag on the objects
  concerned, and states its largest such difference in the rates-bound
  capability (A.3 0x0013). The distance figure is loose on purpose: a distance
  rate that omits the light-time term differs by the observer's acceleration
  times the light time — measured at 3e-5 AU/day for Uranus in the Swiss
  Ephemeris — which is a definitional difference, not an error, and a server
  whose distance rates omit it says so with the flag.

**Sidereal zodiacs** (plane 0 only; see §3.5):
- A zodiac has a **zero point**: a mean ayanamsa A₀ at an anchor epoch t₀ (TT).
  - `user`: t₀ = `anchorEpoch` (TT), A₀ = `anchorAyanamsaDeg`, a **mean**
    ayanamsa (no nutation in it).
  - Named tokens (A.11): the published definition of that mode's zero point. A
    server implements the tokens whose definitions it implements, and advertises
    exactly those.
- **siderealPlane 0 (ecliptic of date).** The ayanamsa subtracted at t is
  A(t) = A₀ + p(t₀, t), p the general precession in longitude from t₀ to t, plus
  the nutation in longitude at t **for frame 0 only** (true ayanamsa); frame 1
  uses the mean ayanamsa; frames 2 and 3 use the constant A(J2000.0) mean, a zero
  point fixed on the J2000 ecliptic.
- **siderealPlane 1 (ecliptic of the anchor epoch).** Positions are referred to
  the mean ecliptic and equinox of t₀; longitude is counted from the zero point
  there (A₀ subtracted, no precession term).
- **siderealPlane 2 (invariable plane).** Positions are projected onto the
  invariable plane of the solar system (the orientation the server names in its
  engine description); longitude is counted along that plane from the zero point
  carried onto it. Servers that implement it advertise it (A.3 0x0008).
- The ayanamsa column reports the value subtracted for the row: A(t) for plane
  0, A₀ for planes 1 and 2.

**Orbit points** (kind 1):
- **Which orbit.** The body's orbit about the Sun (heliocentric) — or about the
  solar-system barycentre for method 3 — and, for the Moon (301), its orbit
  about the Earth.
- **Nodes** lie on the ecliptic of the profile's frame (the mean ecliptic of
  date for frames 0 and 1; the J2000 ecliptic for frames 2 and 3). The node is
  the point on the orbit at that plane crossing, at the orbit's radius there.
- **Apsides** are the points of the orbit at pericentre, a(1−e), and apocentre,
  a(1+e); method 4 answers the empty focus, 2ae from the centre, in place of
  the apocentre.
- **Corrections apply as sent**, as they do to a body, and the three terms are
  defined independently (see **Corrections** above). The answered coordinates
  are the point as seen from the profile's observer, in its frame and plane,
  with the zodiac applied.
  - *Deflection* and *aberration* are as for a body, applied to the direction
    of the point.
  - *Light time* retards the point to the instant its light would have left it.
    An orbit point emits no light, so this is a **convention**, not an
    observable, and two engines may mean different operations by it. Measured
    2026-09-17 against Prometheia: Swiss's light-time term on the Moon's true
    node is 0.0031", theirs is 19.10" -- four orders of magnitude apart in the
    intermediate -- while the two apparent answers agree to 0.0002". Neither
    is wrong; they are different conventions for a point that has none.
- **Corrections on an orbit point are not independently meaningful.** The point
  is a construction rather than an emitter, so a proper SUBSET of the three
  terms is well defined only within one implementation. Two answers are
  interoperable when all three terms were applied, or when none were. A server
  MAY answer a proper subset, and MAY report which terms it applied in META's
  `corrApplied`; a client
  MUST NOT compare such an answer across servers. This is the reason the
  astrometric lunar nodes of two conforming servers may differ by 19", while
  their apparent ones agree to 0.0002".
- **Osculating (1, 3)** elements come from the body's state vector with
  μ = G(M_centre + M_body) from the ephemeris's own constants (M_body 0 where
  unknown).
- **Mean (0)** elements are the server's mean-element model, **named in the
  object's source string** (source strings are
  `<engine> | <ephemeris> | <model>`, the first two fields stable and the third
  free text, e.g. `prometheia 0.1.0 | JPL DE440 | mean elements: DE440 secular fit`) (e.g. "mean elements: DE440 secular fit",
  "Moon: Simon et al. 1994"). Mean points legitimately differ between models.
- **Interpolated (2)** is the Moon's "natural" apogee and perigee, a
  server-defined smoothing of the osculating points, also named in the source.
- **Which bodies.** Any body with an orbit about its centre. The Sun (10) and
  the solar-system barycentre (0) have none: per-object error 2.
- **Method 3 (osculating, barycentric)** takes the elements from the body's
  barycentric state with μ = the ephemeris's total solar-system GM. A server
  whose barycentric elements use another mass does not advertise method 3
  (A.3 0x0005), rather than answer a differently defined point.
- A point undefined for the orbit (a node of an orbit in the reference plane,
  an apsis of a circular orbit) is per-object error 5.

**Fixed stars** (kind 2):
- **Names accepted**, case-insensitive, single spaces:
  - an IAU (WGSN) proper name, e.g. `Aldebaran`;
  - a Bayer designation: a Greek letter as its three-letter IAU abbreviation
    (`alf`, `bet`, …), its English name (`Alpha`) or the Unicode letter (`α`),
    optionally with a component number (`bet1`, `Beta1`, `β¹`), then the IAU
    three-letter constellation abbreviation or its Latin genitive
    (`bet Sco`, `Beta Scorpii`);
  - a Flamsteed number and constellation, `8 Sco`;
  - `HR n`, `HD n`, `HIP n`.
  Servers MAY accept further traditional aliases.
- **Components.** A name that matches more than one star (`Beta Sco` for β¹
  and β²) is per-object error 6, exactly as for designations; a client
  chooses with LOOKUP.
- **Deep-sky objects.** The `deep sky` cap (A.2 bit 9) means only that kind 2
  **also resolves deep-sky designations**; WHICH catalogues is machine-readable
  in the catalogs TLV (A.3 0x000B), one entry each, e.g. `messier`, `ngc`,
  `ic`. A designation from a catalogue the server did not advertise is
  per-object error 1. (Praesepe, M 44, and the Pleiades, M 45, are used in
  astrology and live in the star catalogues; a server with only the Messier
  catalogue advertises the bit and that one entry.) Two notes for clients: a
  Messier number is not necessarily an extended object -- M 40 is a double star
  and M 73 an asterism -- and these objects have no parallax, so they answer
  with `noDistance`.
- **Answered as** the star's position from the catalogue with proper motion,
  parallax and radial velocity propagated to the instant, then the corrections.
  Distance from the parallax in AU; without a parallax, `noDistance` (§3.5).
  `resolvedNaif` is INT32_MIN.

**Named hypotheticals** (kind 3): the token names a body; its orbital
elements are **server-defined** and the object's source string names the set
used. A client that needs a hypothetical body computed from particular
elements, identically on every server, sends it as kind 4 with those elements.

**Elements** (kind 4):
- **Motion is pure two-body Keplerian**: at each instant the polynomial
  elements are evaluated at T = (t_TT − epoch)/36525 and the position is the
  Kepler solution with those elements; no perturbations.
- μ = GM of the centre (Sun or Earth) from the ephemeris's constants, the body
  massless.
- The elements refer to the mean ecliptic and equinox named by `equinox`.
- Light time, deflection and aberration apply as to a body (light time through
  the same two-body motion); rates are as for any body.

**Error text** (META errText, ERROR text) is covered by §3.8: it never quotes
instants, places or request contents. Servers rewrite engine messages that
would.

### 3.6 Extension rules

1. **Fixed layouts do not change within version 4.** New fields and new
   behaviour arrive as TLVs, together with a caps bit or capability TLV when the
   client must know about them.
2. **The version is bumped only when a fixed layout must break.** The layouts
   frozen by §3.3 never break.
3. **Registries (Appendix A) grow without a version bump.** Values are appended
   and never reused.
4. **Neither end sends what the other did not advertise.**
5. **Experimental tags, types 0x7000–0x7FFF and tags 0x7000–0x7FFF** are for
   trials between consenting implementations and MUST NOT ship enabled by default.

### 3.7 Caching

- **The key** is `datasetId` followed by the question block's bytes, exactly as
  received. Canonical encoding (§3.1) makes equal questions equal bytes.
- **Delivery fields are outside the key.** A question asked for f32 in small
  chunks is a cache hit on the f64 answer.
- **Client caches** MUST be keyed on `datasetId` as well, and MUST be dropped
  when the server address changes.

### 3.8 Security and privacy

- **Logs.** Servers MUST NOT log request contents (instants, places, bodies)
  unless the operator explicitly enables it, as `astrolog-ephd --log-contents`
  does. Tokens are never logged.
- **Error text** — ERROR's text and every META errText — never contains
  instants, sites or other request contents; servers rewrite engine messages
  that would (e.g. "outside the ephemeris's time coverage", not "jd 2461300
  outside coverage").
- **Hardening.** Every parser is bounds-checked on truncated input and never
  allocates in proportion to an unvalidated count before checking it against a
  limit.

### 3.9 Performance and accuracy

Both are requirements, and the protocol is built so that neither is bought
with the other. **An engine never trades accuracy for speed silently.**

- **Every approximation is on the wire.** `approximated` (the body answered is
  not the body asked), `extrapolated` (outside the data's fitted range),
  `ratesApprox` with its bound (A.3 0x0013), `noDistance`, a segment's
  `errArcsec` and `errRelDist`, and σ where the engine has it. A server that
  cannot answer a question to its normal accuracy says so per object rather
  than quietly answering something else.
- **f32 is a delivery choice, never a computation one.** Servers compute in
  f64 and round at the last step; the cache holds the f64 answer (§3.7).
- **The batch is the unit of work.** One REQUEST carries a whole cast --
  every object, every instant, every profile -- because a request per object
  costs a round trip each and defeats the server's cache. A client that needs
  a second window before the first is drawn sends it with `priority = 1`, and
  the server answers interactive work first.
- **Segments are the answer to animation** (§3.4): one fit covers a span the
  client then evaluates locally at any instant, with the error it asked for
  stated on the wire.
- **CANCEL exists so that speed is not wasted:** an animation that moves on
  drops the window it no longer needs, and the server stops computing it
  (§3.4: in blocks, and caching nothing from a cancelled request).
- **Share the per-instant work across a cast.** Computing time-major -- every
  object at one instant, then the next -- lets the observer, Sun, frame and
  nutation work be memoised across the objects of a cast. Prometheia measured
  56 → 2.9 µs per object-row from this family of changes: interpolating the
  nutation series from half-day nodes (0.004 µas against the full series),
  caching those nodes, Newton's method for light time, memoising the observer
  and Sun states, and then time-major ordering for a further 17%. The ordering
  is free to try -- the answer cannot depend on it -- but it only pays where
  the per-instant work is memoised rather than recomputed inside the engine.
- **The cache key is canonical** (§3.7), so the same question asked twice --
  in either precision, in any chunking -- is computed once.
- **A request's cost is dominated by the time WINDOW it touches**, not by the
  objects in it, and that cost is shared by everything else touching the same
  window. Measured on Prometheia: the frame work for a year is about 730
  nutation nodes, roughly 15 ms, paid once and then free for every body, every
  instant and every client in that window; per-object costs sit within a factor
  of six of each other (an ephemeris body 14.2 µs, a catalogue body 5.3, a star
  2.4, an orbit point 3.5), most of the spread being one light-time solve. So
  the honest unit for a limit is SPAN, which is what `maxSegSpanDays` measures,
  and the lattice (§3.4) shares not only fitted segments but the frame work
  beneath them.
- **Ask a server for the same instant twice, and ask it in a different
  ORDER.** If the second identical instant costs what the first did, something
  is being recomputed that should have been reused; and if one object costs
  several times another of its kind, find out whether that is the object or
  merely the one asked FIRST -- a per-window cost paid by whoever arrives first
  reads exactly like an expensive body. Prometheia's engine had both: a
  catalogue record decoded per position, and then a "28× more expensive"
  integrated body that turned out to be a planet paying for the window's
  nutation nodes. Each was hidden under a speedup already banked --
  measured on Prometheia's engine, where a catalogue record was decoded per
  position outside the memo everything else amortised into, and looked like a
  28× cost for integrated bodies until it was fixed (1,013 → 5.9 µs, which is
  now cheaper than a DE read). It is a one-line experiment and it finds what
  profiling a realistic workload hides, so each implementation should run it on
  its own engine rather than assume.
- **Nothing is per connection that need not be.** The only state the protocol
  requires a server to hold for a connection is the negotiated version, the
  token's budget and the answers in flight. Everything else is
  content-addressed -- the cache key is `datasetId` plus the question block --
  so any loop, thread or process may serve any request, a reconnecting client
  loses nothing but what was in flight, and a server scales by adding loops
  rather than by remembering clients. A future implementation MUST NOT invent a
  session that answers differ by.
- **Budgets and limits are per connection, not per answer:** a server states
  its bound in WELCOME (`maxCells`) and a client keeps a request under it
  rather than discovering the limit by being refused.

**The three workloads a client brings**, because they want different things:
- **A cast.** 30-80 objects at one instant, in front of a user: latency-bound.
  Round trips hurt; this is where one request per cast matters.
- **Animation.** A window of instants at a fixed step, about 1000 rows, with
  the next window prefetched at priority 1 while the current one draws.
- **Scanning**, the heaviest and the one that shapes segments. Astrolog searches
  for events -- aspects forming, ingresses, stations, voids, eclipses,
  progressed hits -- by sampling uniformly and interpolating between adjacent
  samples: `-d` is 48 divisions a day by default, so a month is about 1500 full
  casts and a year about 4400 (charts3.cpp:270 and the interpolation at :354).
  The event time then comes from LINEAR interpolation between two bracketing
  samples, so its accuracy is limited by the sampling step, not by the
  ephemeris.

For scanning, **segments are an accuracy change, not a bandwidth one**: the
client evaluates any instant locally, so an event time comes from root-finding
on the polynomial instead of interpolating between half-hour samples, and the
answer stops depending on `-d`. Rates from the analytic derivative make a
station -- a sign change of the longitude rate -- exact the same way, which is
why a segment carries a measured rate residual. A fitter should therefore be
judged on whether the polynomial's ROOTS land where the sampled function's
roots would, over spans of a month to a year, rather than on looking smooth;
and where a body needs an unreasonable degree over such a span, more segments
at a sane degree beat a refusal. The Moon decides it: 13° a day, and a
year-long scan of lunar aspects is the commonest heavy search in the program.

This is also why the reserved event-search message types (16-31) may stay
reserved: with segments a client can search correctly for itself, which is a
better place for that complexity than the protocol.

Measured on the reference implementations (`tools/ephsrv-bench.sh`, and
Prometheia's own bench): a cold 30-body 1000-row window is about a
core-second of computation, and a cached one is delivered in milliseconds;
the numbers that matter to a GUI are in EPHEMERIS_SERVER_PRODUCTION_PLAN.md.

### 3.9a How the implementations work together

Two implementations wrote this specification at once, and the rules below are
how they stayed one protocol. They bind any third implementation as well.

These are rules about the **protocol**. How the two *sessions* negotiated it --
the shape of a round, the discipline that turned disagreements into findings,
who decides what, and the standing rule that a peer agent cannot grant
permission -- is `EPHEMERIS_PROTOCOL_COLLABORATION.md`.

1. **Never branch on who the peer is.** No code reads `serverName`, `engine`
   or `clientName` and behaves differently. Anything one end needs to know
   about the other is a capability bit or a TLV, registered in Appendix A and
   advertised.
2. **Advertise only what is implemented and tested.** A capability advertised
   because it mostly works is worse than one absent: the other end will use it.
   A bit whose behaviour is incomplete, or which no test exercises, stays dark.
3. **No silent fallbacks.** An engine that cannot answer the question as asked
   says so per object, with a code; it never quietly answers a nearby question.
   A substitution is visible on the wire (`resolvedNaif`, `approximated`) or it
   does not happen.
4. **Errors by meaning, not convenience.** A failure maps to the A.17 code
   that is true. If none fits, the registry gains one, with a fixture.
5. **No sniffing, no speculative parsing.** The envelope's version and the
   advertised capabilities are the whole truth; nothing is inferred from
   content. A place where guessing is tempting is a gap in this document.
6. **Every rule ships with a fixture or a gate**, in the same change that
   agrees it. A rule that exists only in prose gets implemented two ways.
7. **No leniency for one's own convenience.** Neither end accepts a message
   this document makes malformed because refusing it is inconvenient today; a
   lenient parser hides the other end's bug until a third implementation
   appears. An end that is strict where this document is silent fixes the
   document, not the parser.
8. **The engine does not know about the wire.** The protocol stops at each
   side's boundary and is translated there. A protocol field that reaches into
   engine semantics makes that engine a function of this document's version,
   and every other consumer of that engine inherits it.
9. **An objection is a finding.** A disagreement between two readings is the
   most valuable output either side produces, and it is resolved by changing
   this document until only one reading survives -- never by one side
   accommodating the other's code. What earned its place here came from
   objections: a rule clients could not obey (an unknown match could not be
   skipped without its length), a rule that named values but not the flag
   fields beside them, a checksum whose input had two readings, and a
   capability that would have been advertised for behaviour that could not
   honour it.
10. **A rule that needs a comment to implement is not finished.** If either
    side writes a comment explaining what this document meant in order to make
    code work, that sentence comes back here as a change.

### 3.10 Conformance fixtures

`ephsrv/conformance/` holds complete messages (envelope included) as hex, with
`MANIFEST.tsv` listing:

| column | contents |
|---|---|
| file | fixture name |
| direction | `c2s` or `s2c` |
| type | message type |
| expected outcome | `ok`, `malformed` (ERROR 1) or `unsupported` (ERROR 11) |
| note | one line |

**The set is atomic and verifiable.** A regeneration writes every `.hex`
first and `MANIFEST.tsv` last, and the manifest's header carries

```
# set-sha256 <64 hex digits>
```

the SHA-256 over **the bytes of each fixture file exactly as committed**
(hex digits, line breaks and all, including the trailing newline),
concatenated in the order the manifest's data rows list them. The manifest
itself is not part of the input. The verbatim reading is deliberate: it makes
the line a checksum of the directory as committed rather than of an
interpretation of it, so a file rewritten with different formatting — a
semantic no-op — still shows up, and neither side has to decode anything to
check a set. A reader that computes a different digest has an inconsistent or
half-written set and MUST refuse to report verdicts rather than report wrong
ones.

**Who generates and runs them**
- `tools/ephproto4-fixtures.py` is an **independent reference encoder**, written
  from this section and not from the C++ code. It generates the fixtures.
- Astrolog's codec tests and Prometheia's `server_ephproto_matches_astrolog`
  test both parse every fixture and check the expected outcome.
- An `ok` fixture must re-encode to the identical bytes.
- **What fixtures cannot prove.** A fixture is one message, so the rules about
  sequences — chunks contiguous, ascending and covering every row exactly once;
  segments contiguous to 1e-9 day; an answer following its own request — are
  server-side tests on each side, not fixtures.

## 4. Source plugins

### 4.1 Model

A **source** is anything that answers position questions: a local library
over data files, formulas, or a remote service. Sources live in a
compiled-in table in the shared core (`ephem.h`, `ephem.cpp`; the core
group of `Makefile.srcs`; no `#ifdef QT`), in the house style (`CONST`,
`P(())`, flags as `flag`).

```c
typedef struct _EphSrcDef {
  CONST char *szKey;          // "swiss" -- CLI and settings key
  CONST char *szName;         // "Swiss Ephemeris files" -- dialogs
  CONST char *szDesc;         // one line for the dialog
  CONST EPHPARAM *rgParam;    // declared settings (4.3)
  int cParam;
  flag (*FAvailable)(char *szWhy, int cch);  // compiled, files, transport
  void (*GetCaps)(EPHCAPS *pcaps);           // the WELCOME capability model
  int  (*State)(char *sz, int cch);          // esReady/esConnecting/esFailed + text
  void (*Start)(void);                       // local: open; remote: background connect
  void (*Stop)(void);
  flag (*FSubmit)(EPHQUERY *pq);             // non-blocking; local ones compute here
  flag (*FRead)(CONST EPHQUERY *pq, int iObj, int iRow, EPHROW *prow);
  void (*Hint)(CONST EPHQUERY *pq);          // the next window, for prefetch
  int  (*NLookup)(CONST char *sz, EPHMATCH *rgm, int cMax);
} EPHSRCDEF;
```

**Query and row types**
- `EPHQUERY` is the host-side form of a version 4 question block (§3.4),
  declared in `ephproto.h`, so the wire and the plugins cannot drift apart.
  - Each OBJECT also carries a host-only `nNative`, the source's own id for the
    body, which never goes on the wire. The local Swiss plugin uses it to make
    exactly today's calls.
  - For example: the Moon's nodes through the named `SE_TRUE_NODE` and
    `SE_MEAN_NODE` bodies versus `swe_nod_aps` for custom points; `-Ye b 1` as
    `SE_AST_OFFSET+1`; a `seorbel.txt` index.
- `EPHROW` holds the columns, σ, ayanamsa, errCode, resolvedNaif and the key of
  the source that answered.

**Casting**
- **One pass per cast.** The host builds one query per cast (all objects, their
  profiles and the side calls), submits it to each source it needs, calls
  `EphCollect(msDeadline)` once, then reads.
- **Fallback chain.** The selection is an ordered list (§5.1). For each object
  the host asks the first source in the list that is available, advertises the
  capability, and has not already failed that object in this cast. When a
  source fails an object, the next source is asked for the same rows.
  - One quiet notice per cast says that a fallback served something.
  - The provenance is shown in the Ephemeris Settings status and is available
    to text charts and AstroExpressions.
- **Side calls use the same path**, as instant lists: `RProgArc`, the topocentric
  Sun in eclipses, fixed stars, planet phenomena and asteroid listings.
- **Emulation.** Where today's code emulates a capability, the host keeps doing
  it for any source:
  - geocentric → heliocentric with light time (today's Horizons path,
    calc.cpp:1131);
  - the South Node as the opposite point;
  - placeholder speeds.
- **`FCm*` predicates are replaced** by `FEphSpeeds()` (rates available) and
  `FEphLegacyCast()` (Matrix and None).
- **Matrix and None** keep their **legacy-cast hook**: `ComputePlanets` and
  `ComputeLunar` inside CastChart, plus Matrix dates and houses. They therefore
  stay byte-identical to today.

### 4.2 Built-in sources

| key | module | kind | parameters | notes |
|---|---|---|---|---|
| `swiss` | ephswiss.cpp | local | (uses `-Yi` paths) | Swiss Ephemeris files; bit-exact with today |
| `moshier` | ephswiss.cpp | local | — | analytic; major planets and Moon; always available |
| `jpl` | ephswiss.cpp | local | `file` | Swiss over a JPL DE file (a new parameter; today there is no setting and the default is de431.eph) |
| `prometheia` | ephprom.cpp | local | `ephemeris`, `catalog`, `perturbers` | `#ifdef PROMETHEIA`, detected with `pkg-config prometheia`; C API `prometheia_calc*`, `calc_orbit_point*`, `engine_lookup`; ΔT hook bound to Astrolog's |
| `server` | ephserver.cpp + transport | remote | `url`, `token` | protocol v4; astrolog-ephd or prometheiad |
| `horizons` | ephhorizons.cpp | remote | — | rewritten to take instants and batch per body over the Horizons API |
| `matrix` | matrix.cpp | local | — | Sun–Pluto, Moon, mean node; legacy cast |
| `none` | ephem.cpp | local | — | no bodies; legacy cast |

**Default chain.** When the user names only a primary, sources are appended in
quality order after it: `swiss`, `jpl`, `prometheia`, `moshier`, `matrix`. The
same source is never listed twice, and unavailable sources are skipped at cast
time, not at selection time.

### 4.3 Parameters

```c
typedef struct _EphParam {
  CONST char *szKey;     // "url"
  CONST char *szLabel;   // "Server Address"
  int nKind;             // epkText, epkPath, epkFile, epkUrl, epkToken
  CONST char *szDefault; // "" means the source's own default
} EPHPARAM;
```

All parameters of all sources share one generated index space: `cEphParam`,
with enum names like `epServerUrl`. Values live in
`us.rgszEphParam[cEphParam]`. The command-line key is `source.param`, so
`-bP server.url wss://host`. The dialog's generic rows are built from the
table, and a `epkToken` parameter is masked there.

### 4.4 Remote adapter and transports

**The adapter** is generalised from qtdriver.cpp's server client and lives in
core. It provides:
- the window cache, keyed on the datasetId plus the question;
- the animation prefetch through `Hint`;
- a bounded wait;
- a background connect with a fast ladder: 1 s doubling to 60 s, before and
  after a session;
- a recast when a source becomes ready;
- the once-per-cast soft warning;
- terminal refusals (ERROR 7 and 8) that stop the ladder until the settings
  change.

**Transports** implement `Send`, `Pump(msMax)` and `State`:
- **Qt:** QWebSocket.
- **Win32:** WinHTTP WebSocket (Windows 8 and later, native TLS).
- **Console:** a socket client grown from `ephsrv/eph_wsclient.cpp`, with
  optional OpenSSL.

**What does not exist any more:** the required-server dialog, exit code 86, and
the `-0n` fast-fail path. Nothing pops up while a connection is being made.

## 5. Selection, settings, dialogs

### 5.1 State

`us.szEphemSource` holds the chain (e.g. `server,swiss,moshier`), plus
`us.rgszEphParam[]`. These replace **in place**, in astrolog.h and in data.cpp's
positional initializer, the fields `fEphemFiles`, `nSwissEph`, `fMatrixPla`,
`szEphSrv`, `szEphSrvToken`, `fNoOldCalc` and `fNoNetwork`. The default is
`"swiss"` under `EPHEM`, else `"matrix"`. `settingsfields.h` is regenerated,
and the settings sweeps validate source keys.

Changing the source resets `is.fSwissPathSet`, the warning latch and the
adapter caches.

### 5.2 Command line

| spelling | meaning |
|---|---|
| `-bE <source[,source…]>` | select the chain; idempotent |
| `-bP <source.param> <value>` | set a parameter; `""` restores the default |

**Legacy spellings** keep loading exactly as before. They update a parse-time
shadow {files, n, matrix} the way `NSwb` does today, and the chain is
recomputed from the shadow after each one:
- `-b`, `-bs`, `-bj`, `-bJ`, `-bS`, `-bm`, with their `=`, `_` and `:` forms;
- `-bW` and `-bT` set `server.url` and `server.token`;
- `-0b` and `-0n` are accepted and do nothing (registered as inert in
  `tools/inert_option_audit.py`).

-H documents only the new forms, plus one line naming the older spellings.

### 5.3 Settings writer

The writer emits one `-bE` line plus one `-bP` line per non-default parameter.
There is no order dependence, and `=0b`/`=0n` are not written.
`astrolog.as` is updated. `nrvate.as` is the maintainer's file and is left
alone: its old lines keep loading.

### 5.4 Ephemeris Settings dialog (both builds)

**Where it is defined and wired**
- **Resource.** `dlgEphem` in astrolog.rc (resource.h: dialog 226, command
  40366, controls from 1735).
- **Menu.** Setting → "E&phemeris Settings..." after Calculation Settings.
- **Win32.** `DlgEphem` in wdialog.cpp, a case in wdriver.cpp, a declaration in
  extern.h.
- **Qt.** `ShowEphemDialogQt` in qtdialog.cpp; the menu item in qtdriver.cpp
  `BuildSettingMenu`.

**Controls**
- a primary-source list, where unavailable sources are shown with their reason;
- the fallback order;
- a status line (state and provenance), refreshed by a Win32 timer or a Qt
  signal;
- Connect/Test;
- four generic parameter rows: a label, an edit field, and Browse for paths.

**What leaves Calculation Settings.** The method combo, and the Qt-only
server-address and token rows. `QT_ONLY_ROWS` for dlgCalc is removed from
`tools/rc2qt.py`.

## 6. Appendix A — registries

These registries are also published as **`ephsrv/registries.json`**,
generated from this appendix by `tools/gen-registries.py` and regenerated and
diffed by `make check`; `ephsrv/ephproto_test.cpp` requires the codec's own
constants to agree with it, by name. An implementation may vendor that file
rather than transcribe the lists below. Values are appended and never reused
(§3.6), so a pinned copy stays valid; a copy that is missing values is merely
older than the server it is talking to.

**A.1 Message types:**
- 1 HELLO
- 2 WELCOME
- 3 REQUEST
- 4 DATA
- 5 ERROR
- 6 PING
- 7 PONG
- 8 CANCEL
- 9 LOOKUP
- 10 LOOKUP_RESULT
- 11 SUBSCRIBE*
- 12 UNSUBSCRIBE*
- 13 LIST*
- 14 LIST_RESULT*
- 15 SEGDATA
- 16–31 event searches*
- 32–0x6FFF unassigned
- 0x7000–0x7FFF experimental

(* = reserved, no layout yet.)

**A.2 caps bits** (in HELLO clientCaps and WELCOME caps):
- 0 f32
- 1 zstd
- 2 cancel
- 3 lookup
- 4 instant lists
- 5 priority
- 6 segments
- 7 designations (kind 5)
- 8 ΔT tables (REQUEST TLV 0x8004)
- 9 deep sky (kind 2 also resolves `M n`, `NGC n`, `IC n`)

**A.3 WELCOME capability TLVs:**
| tag | payload |
|---|---|
| 0x0001 | object kinds, u32 bitmask of A.12 |
| 0x0002 | observers, u32 bitmask of A.5 |
| 0x0003 | planes u32, forms u32, frames u32 (bitmasks of A.6) |
| 0x0004 | correction masks per observer: u8 n, n × {u32 observers (A.5 bitmask), u8 mask (A.7)} — each pair says that mask is honoured for those observers |
| 0x0005 | orbit points u32 (A.13), orbit methods u32 (A.14) |
| 0x0006 | extra columns u32 (A.10) |
| 0x0007 | zodiacs: u16 n, n × str8 (A.11 tokens) |
| 0x0008 | sidereal planes u32 (A.8) |
| 0x0009 | time scales u32 (A.9) |
| 0x000A | coverage: u16 n, n × {str8 id, TIME min, TIME max} |
| 0x000B | catalogs: u16 n, n × {str8 id, str8 snapshot} |
| 0x000C | ΔT model: str8 |
| 0x000D | precession models: u16 n, n × str8 |
| 0x000E | rate: u32 cellsPerSec, u32 burst |
| 0x000F | segments: u8 maxDegree, u8 ×3 reserved, u32 maxSegmentsPerObject, f32 minErrArcsec, u32 kinds (A.12 bitmask of what it will fit), u32 maxSegSpanDays (the widest span it will fit in one request; 0 = no stated bound) |
| 0x0010 | lookup: u16 maxMatches |
| 0x0011 | hypotheticals: u16 n, n × str8 (A.15 tokens served) |
| 0x0012 | equinoxes for elements: u32 bitmask of A.16 |
| 0x0013 | rates bound: f32 degPerDay, f32 auPerDay — the largest difference of the server's rates from central differences of its positions (§3.5a); absent means rates meet the 1e-5 °/day, 1e-9 AU/day tolerance |

**A.4 REQUEST TLVs:**
- 0x0003 precession model, str8 (non-critical; an unknown model falls back and
  sets ignoredExt)
- 0x8001 ephemeris pin, str8 (critical)
- 0x8002 catalog pin, str8 (critical)
- 0x8003 datasetId pin, str8 (critical) — the whole identity in one string;
  ERROR 5 if the server's datasetId differs
- 0x8004 ΔT table (critical; requires the `ΔT tables` cap): u32 n (2..65535
  entries allowed by the TLV length), n × {TIME t, f64 deltaTSec}, **instants in
  TT** whatever the request's time scale (§3.5), strictly ascending, all finite.
  When present the REQUEST's `deltaTSec` MUST be the canonical NaN.

**A.5 Observers:**
- 0 geocentric
- 1 topocentric
- 2 heliocentric
- 3 solar-system barycentre
- 4 body

**A.6 Planes, forms and frames:**
- Planes: 0 ecliptic, 1 equator.
- Forms: 0 spherical, 1 rectangular.
- Frames: 0 true of date, 1 mean of date, 2 J2000 (mean), 3 ICRF.

**A.7 Correction bits:** 1 light time, 2 gravitational deflection, 4 aberration.

**A.8 Sidereal planes:** 0 ecliptic of date, 1 ecliptic of the anchor epoch,
2 invariable plane of the solar system.

**A.9 Time scales:** 0 UT1, 1 TT, 2 TDB.

**A.10 Extra column bits:**
- bit 0: σ, arcsec
- bit 1: ayanamsa applied, deg
- bit 2: light time, days
- bit 3: ΔT used, s

**A.11 Zodiac tokens.** The Swiss Ephemeris 2.10.03 sidereal modes are listed
in `SE_SIDM_*` order. Other engines implement whichever subset they choose and
advertise it (A.3 0x0007).

- `fagan-bradley`, `lahiri`, `deluce`, `raman`, `usha-shashi`, `krishnamurti`,
  `djwhal-khul`, `yukteshwar`, `jn-bhasin`
- `babyl-kugler1`, `babyl-kugler2`, `babyl-kugler3`, `babyl-huber`,
  `babyl-etpsc`, `aldebaran-15tau`, `hipparchos`, `sassanian`, `galcent-0sag`
- `j2000`, `j1900`, `b1950`
- `suryasiddhanta`, `suryasiddhanta-msun`, `aryabhata`, `aryabhata-msun`,
  `ss-revati`, `ss-citra`, `true-citra`, `true-revati`, `true-pushya`
- `galcent-rgilbrand`, `galequ-iau1958`, `galequ-true`, `galequ-mula`,
  `galalign-mardyks`, `true-mula`, `galcent-mula-wilhelm`, `aryabhata-522`
- `babyl-britton`, `true-sheoran`, `galcent-cochrane`, `galequ-fiorenza`,
  `valens-moon`, `lahiri-1940`, `lahiri-vp285`, `krishnamurti-vp291`,
  `lahiri-icrc`
- `user` (anchored by the PROFILE's anchor fields)

**A.12 Object kinds:**
- 0 body
- 1 orbit point
- 2 fixed star
- 3 named hypothetical
- 4 elements
- 5 designation

**A.13 Orbit points:** 0 ascending node, 1 descending node, 2 perihelion
(perigee), 3 aphelion (apogee).

**A.14 Orbit methods:**
- 0 mean
- 1 osculating
- 2 interpolated ("natural"; the Moon only in Swiss)
- 3 osculating, barycentric
- 4 focal point

**A.15 Named hypotheticals.** Tokens name bodies; their elements are
server-defined (§3.5a), and the source string names the set. The tokens are
the conventional names of these bodies (listed in the order of Swiss's
`seorbel.txt`, for reference only):

| token | seorbel.txt entry |
|---|---|
| `cupido`, `hades`, `zeus`, `kronos`, `apollon`, `admetos`, `vulcanus`, `poseidon` | Uranians, 1–8 |
| `isis-transpluto` | 9 |
| `nibiru` | 10 |
| `harrington` | 11 |
| `neptune-leverrier` | 12 |
| `neptune-adams` | 13 |
| `pluto-lowell` | 14 |
| `pluto-pickering` | 15 |
| `vulcan` | 16 |
| `white-moon` | 17 (Selena, the T-term form) |
| `proserpina` | 18 |
| `waldemath` | 19 |

A server MAY serve more; each token names one body.

**A.16 Element equinoxes:** 0 J2000, 1 B1950, 2 J1900, 3 of date, 4 explicit
JD (`equinoxJd`). The element epoch is a TIME in TT. Polynomial terms are in
T = (t_TT − epoch) / 36525 Julian centuries, as in `seorbel.txt`.

**A.17 Per-object error codes:**
- 0 none
- 1 unknown body or name
- 2 unsupported by this source (kind, observer, option or body)
- 3 outside the data's time coverage
- 4 data unavailable (for example a missing file)
- 5 point undefined (for example the node of a zero-inclination orbit)
- 6 ambiguous name
- 7 numerical failure
- 8 internal

**A.18 META flags:**
- bit 0 approximated (resolvedNaif differs from the request, e.g. 499 answered
  as 4)
- bit 1 extrapolated
- bit 2 hasSigma
- bit 3 partial
- bit 4 noSpeeds
- bit 5 noDistance (a star without a parallax)
- bit 6 ratesApprox (rates may exceed the §3.5a tolerance; see A.3 0x0013)

**A.19 ERROR codes:**
- 1 malformed or non-canonical
- 2 over a WELCOME limit
- 3 unknown message type
- 4 internal
- 5 source, data or pin unavailable
- 6 rate limited
- 7 token required or unknown
- 8 version
- 9 busy (too many unread answers)
- 10 cancelled
- 11 unsupported (a value, capability or critical extension not advertised)
- 12 draining (retry elsewhere)

**A.21 Element centres** (kind 4 `centre`): 0 Sun, 1 Earth.

**A.20 Precession model tokens** (REQUEST TLV 0x0003, WELCOME TLV 0x000D):
- `iau2006` — Capitaine et al. 2003, IAU 2006 (the default)
- `vondrak2011` — Vondrák, Capitaine & Wallace 2011, long-term

## 6B. Appendix B — mapping to the Swiss Ephemeris

A single shared header, `ephsrv/ephswiss.h`, holds the mapping. Both
astrolog-ephd and Astrolog's `swiss`/`moshier`/`jpl` plugins use it.

**Bodies**
- **Canonical Swiss body first.**
  - 10 → `SE_SUN`
  - 301 → `SE_MOON`
  - 199/299 → `SE_MERCURY`/`SE_VENUS`
  - 399 → `SE_EARTH`
  - 4–9 → `SE_MARS`..`SE_PLUTO` (system barycentres, Swiss's default)
  - 20000001–4 → `SE_CERES`..`SE_VESTA`
  - 20002060 → `SE_CHIRON`
  - 20005145 → `SE_PHOLUS`
  - 20134340 → `SE_PLUTO`, flagged approximated
- **Other numbered asteroids.** 20000000+N → `SE_AST_OFFSET + N`.
- **Body centres and moons.** x99 and moon ids → `SE_PLMOON_OFFSET + naif`.
  199–499 are answered as the barycentre and flagged approximated. 0 and 3 are
  per-object error 2.

**Orbit points**
- On 301, (point 0, method 0) → `SE_MEAN_NODE`, (0, 1) → `SE_TRUE_NODE`,
  (3, 0) → `SE_MEAN_APOG`, (3, 1) → `SE_OSCU_APOG`, (3, 2) → `SE_INTP_APOG`
  and (2, 2) → `SE_INTP_PERG`.
- Everything else → `swe_nod_aps` with `SE_NODBIT_MEAN`/`OSCU`/`OSCU_BAR`/`FOPOINT`.
- The descending node is the ascending node's opposite for the named bodies.
- A host-only `nNative` keeps Astrolog's custom Moon points on `swe_nod_aps`.

**Stars and hypotheticals**
- **Stars:** `swe_fixstar2`.
- **Hypotheticals:** `SE_FICT_OFFSET` + the A.15 index.
- **Elements:** the fork's new orbital-elements entry point.

**Options**
- **Observer** → `SEFLG_TOPOCTR` plus `swe_set_topo`, `SEFLG_HELCTR`,
  `SEFLG_BARYCTR`, or `swe_calc_pctr`.
- **Plane** → `SEFLG_EQUATORIAL`. **Form** → `SEFLG_XYZ`.
- **Frame:**
  - true of date: none
  - mean of date: `SEFLG_NONUT`
  - J2000: `SEFLG_J2000 | SEFLG_NONUT`
  - ICRF: `SEFLG_ICRS | SEFLG_J2000 | SEFLG_NONUT`
- **Corrections.** Swiss can honour the masks 7, 0 (`SEFLG_TRUEPOS`),
  3 (`SEFLG_NOABERR`), 5 (`SEFLG_NOGDEFL`) and 1 (`NOABERR|NOGDEFL`).
- **Speeds** → `SEFLG_SPEED`.
- **Zodiac** → `SEFLG_SIDEREAL` with `swe_set_sid_mode`: the mode from A.11,
  `SE_SIDM_USER` with the anchor, plus `SE_SIDBIT_ECL_T0` or
  `SE_SIDBIT_SSY_PLANE` for sidereal planes 1 and 2.
- **Time scale.** UT1 calls the `_ut` entry points (or TT = UT1 + ΔT when
  `deltaTSec` is given). TT is used directly. TDB is error 2 unless it converts
  exactly.

## 6C. Appendix C — mapping to Ephemeris Prometheia

These are Prometheia's C ABI names; the C++ engine is the same.

**Bodies**
- **Kind 0:** NAIF ids and SBDB SPK-IDs, directly.
- **Kind 1:** `prometheia_calc_orbit_point` with methods 0 and 1.
- **Kind 4:** its Kepler engine, when implemented.
- **Kind 5 and LOOKUP:** `prometheia_engine_lookup`.

**Options**
- **Observer:** 0–3 → `PROMETHEIA_CENTER_{GEO,TOPO,HELIO,BARY}CENTRIC`, and
  4 → `PROMETHEIA_CENTER_BODY` + `center_body`.
- **Plane** → `coords`. **Frame** → `frame` (ICRF, J2000, MEAN_OF_DATE,
  TRUE_OF_DATE).
- **Corrections** → `light_time`, `deflection`, `aberration`.
- **Speeds** → `speed`.
- **Zodiac:** `fagan-bradley`, `lahiri`, `user` → `sidereal` with the anchor.
  Sidereal plane 0 only.
- **Extra columns:** σ → `sigma`, `HAS_SIGMA`; ayanamsa → `ayanamsa_deg`;
  light time → `light_time_days`.
- **Time:** TT → `prometheia_calc`, UT1 → `_ut` with the ΔT hook or
  `deltaTSec`.
- **Datasets:** `datasetId` derives from the DE file, catalogs and perturbers
  loaded; the source strings come from `result.source`.

## 7. Phases

Each phase is one or more commits on `ephv4`, each passing `make check` and
the gates the phase touches.

1. **This document and the conformance fixtures.** Covers §3.10:
   `tools/ephproto4-fixtures.py` and `ephsrv/conformance/`.
2. **Protocol v4 in code.**
   - `ephproto.h`: codecs for every message, TLV, SEGDATA, LOOKUP and CANCEL,
     plus the conformance test.
   - `ephsrv/ephswiss.h`.
   - The Swiss fork's elements entry point.
   - `eph_srv.cpp`: WELCOME capabilities, profiles, LOOKUP, CANCEL, and segments
     fitted from samples.
   - `eph_wsclient.cpp`.
   - The Qt client: one request per cast.
   - Every `tools/ephsrv-*.sh` gate moves to v4, with golden bit-exact against
     the fork's swetest.
3. **Source registry.**
   - `ephem.h`/`ephem.cpp`; the `swiss`, `moshier` and `jpl` plugins;
     `matrix`/`none` as legacy cast.
   - ComputeEphem and the side calls go through the host API with the fallback
     chain.
   - Chart, switch, graphics and influence matrices byte-identical against a
     baseline.
4. **Selection re-plumb:** state, `-bE`/`-bP`, the legacy shadow, the writer,
   the locks, astrolog.as, and the settings sweeps and audits.
5. **Ephemeris Settings dialog** in both builds; Calculation Settings loses the
   combo.
6. **Remote adapter and transports** (Qt, WinHTTP, console socket).
   - `server` and `horizons` plugins.
   - The required-server dialog, exit 86 and `is.fNoEphFound` go away.
7. **Prometheia plugin** (optional dependency, makefile detection, oracle
   against Swiss on DE440).
8. **Review** of the whole branch, and a summary for the maintainer.

## 8. Work log

0. **Spec amendments after Prometheia's review (2026-09-17).** The Prometheia
   maintainers reviewed §3 and Appendices A and C (not B: they are a
   cleanroom) and found eight places where two conforming servers could
   legitimately return different numbers. All are now normative in **§3.5a**:
   rates as derivatives of the answered coordinates with a tolerance and a
   `ratesApprox` flag plus a rates-bound capability; a critical ΔT-table TLV
   (0x8004) because one deltaTSec cannot cover a century-long grid; sidereal
   zodiacs (anchor in TT and mean, which ayanamsa each frame gets, ecliptic
   plane only -- equatorial sidereal is now malformed, the ayanamsa column
   defined); orbit points (which orbit, which plane, corrections as sent,
   mean models named in the source); fixed stars (an accepted name grammar, components
   ambiguous, no deep-sky objects, distance and resolvedNaif); corrections
   honoured as sent for every observer, with per-observer masks advertised
   (A.3 0x0004 gains the observer bitmask); frames defined exactly, with
   J2000 including frame bias and the site on WGS84; privacy covering META
   errText. Also: hypothetical elements are server-defined, and a client
   needing identical numbers everywhere sends kind 4; elements are pure
   two-body with the centre's GM; precession tokens registered (A.20);
   segments note that velocities come from the fit.

0b. **Second Prometheia pass, and performance (2026-09-17).** Their second
   reading found five smaller ambiguities, all now closed: the ΔT table's
   instants are TT and are interpolated at a row's own numeric value with no
   iteration (deterministic on both ends); a sidereal rectangular answer is
   rotated by the same ayanamsa as the spherical one; deep-sky designations
   (`M n`, `NGC n`, `IC n`) are served under kind 2 behind a new `deep sky`
   cap, because Praesepe and the Pleiades are used in astrology and live in the
   star catalogues; orbit points of the Sun and the barycentre are error 2, and
   method 3's mass is pinned (a server using another does not advertise it);
   deflection applies at the barycentre, with a solar-disc guard. They also
   accepted two of ours as improvements on their proposals -- the ratesApprox
   flag over redefining Swiss's speeds, and an ambiguous star being error 6
   rather than the brighter component.
   - New **§3.9 Performance and accuracy**, at the maintainer's instruction
     that both are keys: every approximation is declared on the wire
     (approximated, extrapolated, ratesApprox with its bound, noDistance,
     segment error bounds, σ), f32 is delivery-only and the cache holds f64,
     one REQUEST carries a whole cast, prefetch has its own priority, segments
     answer animation, CANCEL stops wasted computation, and the cache key is
     canonical so one question is computed once.

0c. **Orbit-point corrections, settled with Prometheia (2026-09-17).** The
   agreement in §3.5a above, and the correction of item 2's physics.
   - **The term Swiss carries on a planetary orbit point is ABERRATION, not
     light time.** Prometheia objected to item 2's measurement; re-measured
     rather than defended, and they were right. `SEFLG_NOABERR` alone moves
     Jupiter's ascending node **-20.8370"** (their independent figure
     20.8378"). Item 2 had measured the bits on the lunar *named* bodies,
     where they genuinely move nothing, and carried the sentence to the
     planetary case without re-running it.
   - **Swiss applies zero light time to planetary orbit points.** TRUEPOS and
     NOABERR|NOGDEFL agree to **0.000000"** on Jupiter, Saturn, Mars and
     Pluto, by both methods. Deflection is honoured (0.0002-0.008"),
     aberration is honoured (1-21"), light time is inert. So on kind 1
     planetary this server structurally cannot honour the light-time bit.
   - **The lunar points are the other way round**, settled two independent
     ways. By code: `lunar_osc_elem()` (sweph.c:5692, reached from :976 for
     SE_TRUE_NODE and :1000 for SE_OSCU_APOG) consults exactly one flag,
     SEFLG_TRUEPOS, and `grep -c "swi_aberr_light\|swi_deflect_light"` over
     it returns **0**. By behaviour: SE_MOON itself moves +11.361744" under
     NOABERR, so the flag is live on the lunar path and only the node
     function never asks. Lunar points therefore carry light time, and
     neither aberration nor deflection.
   - **A hypothesis neither side had named** was killed in passing: that
     NOABERR might be dead on the whole lunar path, which would have given
     the same 0.0000" while aberration *was* being applied. The SE_MOON
     measurement above rules it out.
   - **The 19.1" astrometric gap is a spec carve-out, not a bug.** Our
     astrometric lunar nodes equal our apparent ones to the bit; theirs are
     19.105" away; the apparent answers agree to 0.0002". Both servers are
     truthful. §3.5a now says corrections on an orbit point are interoperable
     in full or not at all.
   - **Declined: patching vendored Swiss's `lunar_osc_elem()`** to retard in
     the barycentric frame, which would close the 19.1" at a cost of 0.0002"
     on Astrolog's published apparent nodes. Three reasons, all accepted by
     Prometheia, who withdrew the suggestion: vendored Swiss must reproduce
     Swiss, and the golden gate compares against the fork's own swetest, so
     patching would make that gate measure us against ourselves; "light time
     to a node" is a convention, so adopting their route into a reference
     implementation is a convention change dressed as a bug fix; and it is
     the maintainer's call, not one to take on a peer's suggestion.
   - **A retraction of our own.** We had written that the golden leg could use
     the applied-corrections report to decide which objects are comparable.
     Prometheia showed it cannot: the two lunar answers agree to 0.0002" with
     DIFFERENT corrections applied, and the mean nodes will differ with
     IDENTICAL ones. It explains a difference; it does not predict one, and
     equality of it is neither necessary nor sufficient for two answers to
     agree. A conformance harness MUST NOT gate comparisons on it.
   - **Standing fact for the golden leg:** mean orbit points need a looser
     tolerance than osculating ones (their mean rows differ by 0.006-0.025"
     against 0.0003-0.012" osculating), because mean-element fits differ
     between engines. Unrelated to corrections; the reason belongs written at
     the tolerance.
   - **Instrumentation finding, and it is general:** compare angular
     SEPARATIONS, never ecliptic-longitude differences. A 0.063" "gap" read
     as a real disagreement and was entirely the projection -- the Moon's
     apparent-vs-astrometric displacement is 11.424861", its longitude
     difference 11.361744", and the Moon sits at 5.17 degrees latitude.
     Use `acos(sin b1 sin b2 + cos b1 cos b2 cos(l1-l2))`.

4. **The corrApplied drop, encoded and sent (2026-09-17).** Item 1 of the
   drop list above, plus the §3.4 withdrawal (item 2), the regenerated
   fixtures (item 3) and the set sent to Prometheia (item 4). One commit.
   - **Every encoding fact was re-measured live before it was encoded** --
     a C probe against the fork's own libswe.a (`/nvm/work/corrprobe.c`),
     the same `_r` entry points the server calls, at one instant under
     explicit iflags. The per-call classification that `CorrectionsLive()`
     encodes, each row measured: `swe_calc_pctr` honours all three terms
     as asked (aberration from Jupiter's centre 8.77"); `swe_nod_aps`
     never applies light time (its underlying positions always carry
     SEFLG_TRUEPOS; only aberration and deflection are applied afterwards),
     forces deflection off for the Moon and aberration off for a
     non-heliocentric one; the Moon's named osculating and interpolated
     points (`lunar_osc_elem`) carry light time only (TRUEPOS vs default
     differs on the true node, the osculating apogee and both interpolated
     points, not under NOABERR or NOGDEFL); the mean lunar elements are
     analytic and carry nothing; the fixstar path applies deflection and
     aberration and no light time at all; and `plaus_iflag()` narrows a
     heliocentric or barycentric body to light time only. Two of the 0c
     sentences survived re-measurement; one swetest letter trap did not
     survive the writing of it -- swetest's `-p` letters J..Z are the
     FICTITIOUS bodies (Jupiter is the digit 5), and a first battery
     measured Cupido and Vulkanus where it named Jupiter.
   - **The field** shifts `resolvedNaif` and `firstFailedRow` by one in
     DATA's META fixed part, in `ephsrv/ephproto.h` (both directions),
     populated in `eph_srv.cpp` as the profile's mask intersected with
     `CorrectionsLive()` (ephswiss.h). Diagnostic only; unknown high bits
     reserved, tolerated and never refused (a fixture carries 0x87 for
     exactly that).
   - **§3.4 resolved as drafted:** the early-flush sentence is withdrawn;
     a server MUST NOT flush each block as it completes, because META's
     `rowsOk`, `firstFailedRow` and `partial` are facts about the whole
     answer. This server already holds blocks and streams when the answer
     is whole. Put to Prometheia as an explicit question with the drop.
   - **Fixtures regenerated** (92 files, every DATA-carrying one moved);
     new set-sha256
     `1c934c7da19965f21ded99a0a53e36eaa3c45434cfbfaeabddafaf154ea454ec`,
     recomputed independently from the on-disk bytes and equal.
   - **Sabotage-proven:** flipping one corrApplied bit in `ReadMeta` fails
     the codec test 7 ways (every fixture re-encode plus the dedicated
     round-trip check); restored, 311 checks and 91 fixtures pass.
   - **Gates:** golden 149 comparisons bit-exact; ROBUST PASS; make check
     all clear, suite 5772 passed, 0 failed. The suite's first run failed
     the live group with 58 connection failures -- its own freshness check
     had the reason: "astrolog-ephd is newer than its sources". A stale
     server still speaking the old META layout reads exactly like a
     protocol bug; `make ephsrv` and everything passed.

3. **The five debts of the protocol pass, cleared (2026-09-17).** What phase 2
   deferred, and the five §3 rules written after its spec freeze.
   - **`ephsrv/registries.json`**, generated from Appendix A by
     `tools/gen-registries.py` and diffed by `make check` like every other
     generated table. 23 registries -- message types, caps bits, both TLV
     tag spaces, observers, planes, forms, frames, correction bits, sidereal
     planes, time scales, extra columns, object kinds, orbit points and
     methods, element equinoxes and centres, per-object error codes, META
     flags, ERROR codes, the zodiac, hypothetical and precession tokens.
     `ephproto_test` then requires the CODEC's constants to agree with the
     file, by name and not by position, so prose, file and code cannot
     drift; sabotaging one message type's value and one zodiac token was
     each seen to fail it. The file is the other implementation's to vendor,
     so it carries nothing of Astrolog's.
   - **Batched LOOKUP.** LOOKUP carries `u8 nQueries` and that many `str8`
     queries, LOOKUP_RESULT `u8 nQueries` and one match list each, and
     `maxMatches` is the budget for the WHOLE message, filled in query order
     with `truncated` when it runs out. A per-query budget would let a
     260-byte message ask for 255 x 65,535 matches, and a body picker asking
     one name at a time is a round trip a name -- Object Selections offers
     78. Codec, server, wire client (`--lookup`, repeatable) and fixtures;
     the robust gate's L1 asks four names in one message, checks a query
     with no match keeps its (empty) place, and checks that three queries
     under a budget of two come back truncated.
   - **`u32 deadlineMs`** closes the delivery block, which is 16 bytes now.
     Advisory and outside the cache key: this server records and logs it and
     never fails a request for it, because it has one strategy -- samples,
     computed now -- and so no cheaper one to choose.
   - **`u32 maxSegSpanDays`** appended to the segments capability (0x000F),
     codec and fixtures only: nothing here fits segments, and the capability
     is not advertised.
   - **The two fitter-side rules** -- the check set including both endpoints
     (τ = ±1), and the 32-day J2000 lattice with downward-only quantisation
     of `segTargetErrArcsec` -- have nothing to implement in a server that
     does not fit, and are written where the fitter will go (`UnservedOf()`
     in `eph_srv.cpp`), with the measurement that makes the endpoints
     non-negotiable. Nothing in this tree claims to fit anything.
   - **Block-wise computation, and the `cancel` capability.** A REQUEST is
     computed in blocks of rows across turns of its loop, so a CANCEL stops
     the work rather than only the bytes, a cancelled request caches
     nothing, and one large request no longer holds its loop. Measured, 8
     bodies x 20,000 rows: 1420 ms of server CPU answered against 200 ms
     cancelled 200 ms in, and a one-row request on a second connection
     answered in 29 ms where it had waited 1132 ms. `tools/ephsrv-robust.sh`
     C2 gates the three, each seen failing under sabotage.
   - **Measured against §3.9, and it does not hold here.** "Computing
     time-major -- every object at one instant, then the next" is 46% SLOWER
     on this engine: a 30-body 1000-row cold window costs 463 ms that way
     against 317 ms object-major, because Swiss's caches are per body and
     want consecutive instants. So a block is a range of rows of ONE object.
   - **The §3.9 canary, run on this engine.** *The same instant asked 1000
     times against 1000 distinct instants*, one object, server compute time:
     Sun 0.07 ms against 8.4 ms, Moon 0.05 against 9.2, Mars 0.07 against
     9.3, Chiron 0.10 against 12.0, the Moon's true node 0.06 against 11.7.
     A repeated instant costs about 1% of a fresh one -- Swiss's per-body
     cache answers it -- so nothing is being recomputed there. **The
     exception is fixed stars**: Aldebaran costs 3.37 ms for the same
     instant 1000 times and 3.33 ms for 1000 distinct ones, the same number.
     Per-position work that does not depend on the instant is redone for
     every row, which is exactly the shape §3.9 warns about, though the
     absolute cost is small (3.4 µs a row, against 8.2 for a planet).
     Recorded as the next pass's item, not restructured in this one.
     *One object asked first against later*: costs are additive and order
     does not matter. Mars alone 8.8-10.6 ms, Chiron alone 11.2-12.5, the
     two together 19.4-22.0 either way round; eight bodies together 68.5-69.8
     against 69.5 for the sum of the same eight asked alone. No per-window
     cost is paid by whoever arrives first here, and no body is expensive
     merely for being asked first -- the one that is dearer (Jupiter) is
     dearer alone too. That is the other side of the time-major measurement
     above: there is nothing shared across a cast to reuse, because Swiss
     recomputes what it needs per body.
   - **A conflict in §3.4 worth fixing.** The CANCEL paragraph says flushing
     each block as it completes "starts the client's first rows sooner", but
     DATA's rules say chunk 0 MUST carry the metadata and that clients use
     chunk 0's -- and META's `rowsOk`, `firstFailedRow` and `partial` are
     facts about the whole answer. A server cannot stream a block before the
     last row is computed without writing those before they are known. This
     server computes in blocks and streams when the answer is whole, which is
     what the normative rules require; the suggestion needs either a
     "metadata may be revised on the last chunk" rule or withdrawal.

2. **Phase 2, protocol version 4 in code (2026-09-17).** `astrolog-ephd`,
   `eph_wsclient` and the Qt client speak version 4 and nothing else, in one
   commit, because the break is clean and the suite's live group casts
   through both ends at once.
   - **One header.** `ephsrv/ephproto4.h` became `ephsrv/ephproto.h`
     (namespace `eph`), and the version 3 header of that name is gone. What
     remains of version 3 is the refusal an older client can read:
     `EncodeLegacyError` writes ERROR 8 in that client's own layout and
     envelope version, which both the limits gate and the robust gate ask
     for by name. `eph::Object::nNative` moved OUT of the protocol header
     into `MapObject`'s parameters: the header is a pinned interface the
     other implementation vendors, and an Astrolog-only hint does not belong
     in it. `tools/ci-assert-vendorable.sh`, in `make check`, compiles the
     header alone under C++20 `-Wall -Wextra -Werror` from a directory where
     no other header of this tree exists.
   - **The server.** WELCOME carries the capability TLVs that describe what
     it does; REQUESTs are parsed canonically by the shared codec, then
     checked against what it serves (ERROR 11), its limits (2), its budget
     (6), its queue (9) and its drain (12). Each object is mapped through
     `ephswiss.h` and computed on the fork's `_r` entry points, per-object
     failures carrying an A.17 code. LOOKUP resolves the bodies Swiss serves
     by name, the Moon's named points, the A.15 hypotheticals, numbered
     asteroids and fixed stars. CANCEL drops the unsent chunks and answers
     ERROR 10. The operational half is untouched.
   - **Advertised is promised.** The caps bits are f32, lookup, instant
     lists, priority, designations and delta T tables -- each exercised by a
     gate. NOT advertised: `cancel` (the message works, but 3.4 also means
     the server stops computing, and this one computes a whole request in
     one loop callback), segments, elements (kind 4 answers per-object error
     2), deep sky, zstd, and orbit method 3.
   - **The Qt client** builds ONE REQUEST per cast, with a profile per
     distinct Swiss call `FSwissPlanetSpec()` decides on and an object per
     body -- `ephswiss.h`'s `ProfileFromSwiss` and `ObjectFromSwiss`, whose
     round trip through `MapObject` the codec test checks over 681
     combinations. It splits into more requests only when WELCOME's maxObjs
     or maxProfiles says to. Window cache keys are the server's own (3.7):
     the datasetId and the question block's bytes. Animation prefetch
     windows go out with `priority = 1`; an evicted in-flight window is
     CANCELled. The suite's live group casts every scenario through the real
     server and compares the arrays as bytes: all of them are bit-identical
     to the local Swiss path, as they were under version 3.
   - **Two measurements the specification asked for.**
     - *Orbit points are not geometric in Swiss.* Measured at J2000 with
       the bundled ephemeris: switching SEFLG_TRUEPOS on moves
       `swe_nod_aps`'s ascending node of Jupiter by 5.8e-3 degrees (mean and
       osculating alike), its perihelion by 7.5e-4, and the named lunar
       bodies by up to 2.5e-5 (SE_OSCU_APOG). So honouring §3.5a's rule as
       it then stood would mean answering a different point from the one
       Astrolog's local cast computes. The server therefore honours the bits
       as sent, and this was reported rather than kludged: the spec follows
       the engines. **The conclusion held; the physics cited for it did
       not** -- see item 0c. This entry said "SEFLG_NOABERR|NOGDEFL alone
       moves nothing" and inferred that the term Swiss carries is light
       time. Both halves are wrong for planetary points, and the sentence is
       kept here only so the correction has something to point at.
     - *Rates against central differences.* Swiss's speeds, against central
       differences of its own positions over +-0.001 day (and +-0.01 and
       +-0.0001, to tell real disagreement from differencing noise):
       longitude rates agree to 2.4e-6 deg/day (the Moon; the Sun 2.4e-7),
       inside 3.5a's 1e-5. Distance rates do not: Mars 2.2e-6 AU/day, Chiron
       1.7e-5, Pluto 4.7e-5, stable across all three step sizes. So the
       rates-bound capability (A.3 0x0013) is advertised at 3e-6 deg/day and
       1e-4 AU/day, and every object answered with speeds carries
       `ratesApprox`.
   - **What Swiss does with the correction masks, per observer.**
     `plaus_iflag()` turns aberration and deflection off inside every
     heliocentric, barycentric and planet-centred call to `swe_calc`, so for
     a BODY the masks 7, 3, 5 and 1 all answer as 1 there; `swe_nod_aps`
     reads the bits itself and honours them. One per-observer pair cannot
     say both, and A.3 0x0004 is per observer rather than per kind, so every
     mask is advertised for every observer and the narrowing is recorded
     here and in Appendix B.
   - **The gates** all moved to version 4 and pass: golden (149 columns
     bit-exact against the fork, over instants, time scales, a delta T
     table, an instant list, every zodiac plane, every observer, frames,
     forms, correction masks, orbit points and methods including the focal
     point, fifteen hypotheticals, a designation, and the ayanamsa and delta
     T columns), robust (the version refusals, every c2s conformance
     fixture answered as its manifest says, CANCEL, priority ordering,
     LOOKUP, and the version 3 findings), cache (the key's split, now on
     datasetId + question bytes), limits, ops (including that a per-object
     error text carries no instant), soak, tls and bench. `eph_wsclient` was
     rewritten around profiles, kinds and the new options the gates need.
   - **Found while doing it.** `Makefile.ephsrv` never included
     `eph_wsclient`'s dependency file, so a change to `ephproto.h` rebuilt
     the server and left the client linked against the old layout -- which
     reads exactly like a protocol bug in whichever end you did not suspect.
     Fixed with the client's own `.d` in the include list.
   - **Deferred, with reasons.** Segments (SEGDATA is in the codec and the
     fixtures; no server implementation, and the capability is not
     advertised); orbital elements (kind 4 answers per-object error 2 until
     the fork has an entry point); block-wise computation across loop turns,
     which is what makes CANCEL worth advertising and what stops one large
     request blocking its loop (EPHEMERIS_REVIEW.md S4 measured 13.7 s for
     64 x 20000 cells); a `registries.json` generated from Appendix A; and
     the server's object-major loop, where Prometheia measured 56 us to
     2.9 us an object-row by computing time-major and sharing the observer,
     Sun, frame and nutation work across a cast.

1. **Phase 1, the document (2026-09-17).** Written from the approved plan, three
   code surveys (state and command line, GUI, connection) and a design review
   against both codebases.
   - The review caught four errors in the first draft:
     - numbered asteroid SPK-IDs are 20000000+N, not 2000000+N;
     - the sidereal model needed a separate plane field;
     - a cast needs per-object profiles;
     - the coords field had to split into plane and form.
   - `seorbel.txt` confirmed element polynomials in T = Julian centuries from
     the epoch, up to T⁴, with J1900/B1950/J2000/JDATE equinoxes.
