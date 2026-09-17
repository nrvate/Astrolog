# Running astrolog-ephd

`astrolog-ephd` is the Astrolog ephemeris server: Swiss Ephemeris positions
over a WebSocket, for Astrolog's "Ephemeris Server" backend. This directory
is how to run it somewhere other than a developer's machine. The design is
`EPHEMERIS_SERVER_PLAN.md`, the road to production
`EPHEMERIS_SERVER_PRODUCTION_PLAN.md`; its §11 runbook lists every option.

| File | What it is |
|---|---|
| `SWISSEPH_PIN` | the Swiss Ephemeris fork commit the server is built against |
| `Dockerfile` | a container image, built by `tools/ephsrv-image.sh` |
| `astrolog-ephd.service` | a hardened systemd unit for a plain host |
| `certbot-deploy-hook` | copies a renewed certificate to the server and reloads it |

## Build

The server links the thread-safe Swiss Ephemeris fork, a separate
repository. Fetch and build the pinned commit, then the server:

```sh
sudo apt install g++ make git libssl-dev zlib1g-dev
tools/ephsrv-fork.sh /opt/swisseph
make ephsrv SWE_HOME=/opt/swisseph    # ./astrolog-ephd and ./eph_wsclient
```

Or the container image, from a commit rather than the working tree:

```sh
tools/ephsrv-image.sh                 # tags astrolog-ephd:<commit>; proves it
```

## Ephemeris data

The server answers from whatever `--ephe` names and nothing else. Every
body a chart asks for must have its file there, or that body fails with
ERROR 5. Astrolog's bundled `ephem/` (19 MB, 1800-2400) is enough for the
common charts; a full `/swe`-style set covers everything.

## A plain host with systemd

```sh
useradd --system --no-create-home --shell /usr/sbin/nologin astrolog-ephd
install -m 0755 astrolog-ephd /usr/local/bin/
install -d /var/lib/astrolog-ephd
cp -r ephem /var/lib/astrolog-ephd/ephe            # or the full set
certbot certonly --standalone -d ephem.example.org  # before the server holds 443
install -m 0755 ephsrv/deploy/certbot-deploy-hook \
  /etc/letsencrypt/renewal-hooks/deploy/astrolog-ephd
RENEWED_LINEAGE=/etc/letsencrypt/live/ephem.example.org \
  /etc/letsencrypt/renewal-hooks/deploy/astrolog-ephd   # the first copy
install -m 0644 ephsrv/deploy/astrolog-ephd.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now astrolog-ephd
```

Renewals then run the hook themselves. certbot's standalone renewal needs
port 80 free, which this server never uses.

Options beyond the unit's go in `/etc/default/astrolog-ephd`:

```sh
EPHD_ARGS=--cells-per-sec 20000 --max-conns-per-ip 32
```

## A container

```sh
docker run -d --name ephd -p 443:47190 \
  -v /srv/ephe:/ephe:ro -v /etc/astrolog-ephd/tls:/tls:ro \
  astrolog-ephd:<commit> --port 47190 --ephe /ephe \
  --tls-cert /tls/fullchain.pem --tls-key /tls/privkey.pem
docker kill -s HUP ephd      # after a renewal
docker stop -t 15 ephd       # drains, then exits 0
```

## Operating it

- **Health**: `GET /healthz` (the process answers), `GET /readyz` (send it
  traffic: 503 with the reason while draining or with no ephemeris).
- **Metrics**: `GET /metrics`, Prometheus text. Worth alerting on:
  `/readyz` not 200; `ephd_errors_total{code="4"}` or `{code="5"}` rising;
  `ephd_refused_connections_total` and `ephd_errors_total{code="6"}` rising
  (limits biting -- raise them or find the client); the certificate's expiry,
  which the server logs at every start and reload.
- **Upgrading**: start the new binary on the same port while the old one
  drains? No -- the server refuses a port another server is listening on.
  Stop (drains, clients reconnect on their own), replace, start.
- **Logs**: stdout, one line an event. Nothing in them names a request's
  instant, place or bodies, or a token.
- **Clients**: an Astrolog whose protocol is older than the server's oldest
  (`kProtoMin`) is told to update and stops retrying; an older server than
  a client speaks gets the client's retry ladder.

## What is not here yet

The client's default address still points at `localhost`; making a public
server the default is Phase 6 of the production plan, after a load test.
The unit and the hook have been checked for syntax
(`systemd-analyze verify`, `sh -n`), not run on a real host.
