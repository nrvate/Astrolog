# Client/server alignment review — Astrolog and Ephemeris Prometheia

**Question (maintainer, 2026-09-29):** do all the client/server pieces
between the two projects align correctly?

Run jointly by this project's session and the Ephemeris Prometheia session,
each reviewing its own side and cross-checking the other's by behaviour only
(the cleanroom holds: numbers, requests and spec sentences cross, source
does not). The model is `EPHEMERIS_REVIEW.md`: this ledger is the working
state, and every finding ends **fixed** (with the net that fails without
the fix), **closed** (with the killing evidence), or **deferred** (with a
date and a reason).

**Pinned:** Astrolog `4844fac` (qt), Prometheia `dd3d14d` (initial).

## The pieces, and who reviews which

| # | Piece | Where it can disagree | Reviewer |
|---|---|---|---|
| W | The wire artifacts: `ephproto.h`, `registries.json`, conformance fixtures, and each project's vendored copy | a codec rule one side has and the other lacks | Astrolog |
| S | §3 and Appendices A and C as text, against both codecs and both servers | a normative sentence neither implementation follows | joint |
| C1 | Astrolog's `server` source (Qt transport, `ephreq.h`) against `prometheiad` | a question Astrolog asks that their server answers differently | Astrolog, their daemon |
| C2 | Prometheia's client (`crossrun`) against `astrolog-ephd` | the reverse | Prometheia, our daemon |
| P | The `prometheia` source plugin (`ephprom.cpp`) against their C API | an option the plugin maps other than their header documents | Prometheia reads the plugin; Astrolog measures |

## Ledger

| ID | Finding | State |
|---|---|---|
| W1 | Prometheia's vendored `third_party/ephproto/v4/ephproto.h` predates the site drop (`7cbf0d8`, 2026-09-18): ours refuses `siteHeightM <= -6356752` as malformed, theirs decodes it. Their engine fixed the NaN separately (`b48fcf4`); the codec was never re-vendored. `registries.json` matches. | **fixed** Prometheia `2906ecc`: re-vendored (`b6cb5742…`), fixtures re-pinned (`07438c7f…`), both readers 109/109. Their drift check existed but SKIPPED unless `PROMETHEIA_ASTROLOG` was set, and nothing set it; `tools/scheduled.sh` now runs it |
| S1 | §3.5a kind 4 says μ is "GM of the centre from the ephemeris's constants"; the verdicted kind-4 drop says it is the Gaussian constant ("simply wrong", plan line ~1143). The normative text was never amended. | **fixed** — written into §3.5a as agreed (k² heliocentric, k²/332946.050895 geocentric) |
| S2 | §3.5a says nodes lie on "the ecliptic of the profile's frame (… the J2000 ecliptic for frames 2 and 3)"; the amendment both maintainers approved (registry §4.2) is the mean ecliptic of date for every frame. | **fixed** — written into §3.5a word for word |
| S3 | The rate tolerance an absent 0x0013 implies: Appendix A.3 says 1e-9 AU/day, §3.5a says 1e-6 AU/day. | **fixed** — the combined sentence, approved by both maintainers 2026-09-29, written into §3.5a and A.3. **Implementation follows (FIXED 2026-09-29):** body rates are now differenced in the server and in Astrolog's local path, `ratesApprox` stays only on fixed stars and osculating points, and `tools/ephsrv-rates.sh` grades distance as miss / max(1 AU, r) with defaults 1e-5 °/day and 1e-9. Over 1875 object-series the worst was 2.0e-5 °/day and 8.1e-6 relative, both on an osculating point (Mercury's osculating perihelion), which carries `ratesApprox`; every unflagged object meets the defaults. The advertised A.3 0x0013 becomes 5e-5 °/day and 2e-5 (twice the measured worst). Work log item 37 |
| S4 | A.3 0x0014 describes `u32 kinds (A.4)`; object kinds are A.12 (A.4 is REQUEST TLVs). | **fixed** — `registries.json` moves two payload strings (this one and S3's 0x0013 row), sent to Prometheia as a named drop |
| S5 | The site-height rule (`siteHeightM` at or below −6356752 m is malformed) is in the codec and fixtures, not in §3's text; Prometheia's independent reader had to learn it from a header comment. | **fixed** — written into §3.5 |
| C1a | No gated leg runs Astrolog's real `server` source against `prometheiad`. By hand (`ASTROLOG_EPHSRV_URL`, their `dd3d14d`, `-Yi1 ephem`): 60 pass, 34 fail. Every bit-identity check fails, as it must against a different engine; the first differing object in each leg is within 0.0012" in longitude and 0.015" in latitude. The group reports only the first object per leg, so this is a sample, not a maximum. | open |
| C1b | Same run: animation frames miss the 5e-5 tolerance on the Uranians' DISTANCE speed (Vulkanus 1.21e-4, Poseidon 1.45e-4). Which engine's distance rate is right for a hypothetical body is not established. | **closed — ours.** Measured 2026-09-29 against our own server (`vulcanus`, `poseidon`, `cupido`, `hades`; J2000 and 2026; h = 1/1024): our reported distance rate misses the derivative of our own distances by up to 1.33e-4 AU/day (about 1.7e-6 per AU), latitude rate up to 9.7e-6 °/day, longitude rate under 4e-7. Each distance miss is within Earth's acceleration times the point's light time, which fits Prometheia's hypothesis as a bound; it doesn't prove the mechanism. Registry §2.6–2.8's class. Whether to difference our rates or advertise the figure is the maintainer's decision. **Decided and FIXED 2026-09-29:** body rates are differenced from the answered positions (`h` = 1/1024 day, five-point, `EphNodRateDiff()` in `ephsrv/ephnodrate.h`) in `astrolog-ephd` and in Astrolog's local path, so they stay bit-identical; the rates gate now covers `cupido`, `vulcanus` and `vulcan` |
| C1c | Same run: the restart, reconnect and `wss://` legs FAIL under `ASTROLOG_EPHSRV_URL`. They restart a daemon the group did not start, so they cannot apply; they should say so and skip, not fail. A harness defect on our side. | **fixed** — both skip with a printed reason under `ASTROLOG_EPHSRV_URL`; the group's own run still counts 97/97, and against `prometheiad` only engine differences remain (57 pass, 24 fail, all bit-exactness or C1b) |
| T1 | `SphDistance()` is acos-based, so it returns exactly 0 below about 0.003" (the work log's own 2026-09-18 correction). The host-path star check asserts `< 0.001"` with it, so it cannot fail for any difference under the floor. Every sub-0.01" assertion using it has the same blind spot. | **deferred** 2026-09-29 to `STARS_BACKLOG.md` item 9: the one blind assertion is a star check, and fixed stars are parked |
| P1 | The plugin refused every asteroid numbered 119000 or higher, and Pholus, so Swiss answered 16 bodies with Prometheia primary. | **fixed** `4844fac`; the `prometheia` group asks all 69 non-Uranian Object Selections bodies of this source alone, seen failing on both sabotages |
| P2 | Every cast through the plugin left 3 of Astrolog's allocations unfreed at exit. | **fixed** `ec0b288`; the group's allocation check, seen failing with the release removed |
| P3 | Prometheia's analytic star distance rate (`dd3d14d`), checked from this side with our own five-point stencil over their distances: 16 Polaris cells (two dates, Quito and Zurich, ΔT 0/30/69.2/140), every miss at or under 6.6e-7 AU/day, at the stencil's floor. | **closed** — confirms their change |
| P4 | Nessus: Prometheia and Swiss 6.7" apart. | **closed** — two orbit solutions inside JPL's own 3σ; registry §4.5 |
| C2 | Prometheia's `crossrun.py` against `astrolog-ephd` on :47392 (the 2026-09-20 binary, `9635191d`, restarted unchanged). | **done** 2026-09-29 — Prometheia's crossrun against our daemon, record `docs/crosstest/2026-09-29w.tsv` (their repository), 3758 rows, identical verdicts to their record v. It found the topocentric Moon cell, C2b |
| C2b | The C2 run: a topocentric Moon's longitude rate misses the derivative of its own positions by 6.607e-3 °/day at JD 2415020.5, ΔT 140 s, Quito (found by Prometheia's `ratesweep`). Registry §2.6 (Swiss's speed is the rate of a less-corrected quantity than the position it returns). | **fixed** 2026-09-29 — body rates are differenced from the answered positions in the server and in Astrolog's local path (work log item 37); the rates gate's ΔT axis (0, 30, 69.2, 140 s at every observer and epoch) covers the cell |
| C1d | Prometheia's error contract against our wire client (`tools/crosstest-prom.sh`, their `dd3d14d`): every case agrees; grid, list and one-row chunks agree; CANCEL ends a request; segments reproduce their rows (0.000636"). | **closed** — no disagreement |
