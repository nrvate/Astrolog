# astrolog-ephd, the Astrolog ephemeris server, as a container image.
#
# Built by tools/ephsrv-image.sh from a git archive of a commit -- never
# from a working tree -- so an image is exactly one commit of this
# repository plus the Swiss Ephemeris fork commit ephsrv/deploy/SWISSEPH_PIN
# names. The ephemeris data is NOT in the image: mount it at /ephe (the
# bundled ephem/ or a /swe-sized set; EPHEMERIS_SERVER_PRODUCTION_PLAN.md
# decision 0.4).
#
#   docker run -d -p 47190:47190 -v /path/to/ephe:/ephe:ro astrolog-ephd
#
# TLS: mount the certificate and key and append
#   --tls-cert /tls/fullchain.pem --tls-key /tls/privkey.pem
# to the arguments; `docker kill -s HUP` reloads them. `docker stop` sends
# SIGTERM, which drains (answers in flight finish, then exit 0); give it
# more than --drain-seconds (10) with `docker stop -t 15`.

FROM debian:12 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ make git ca-certificates libssl-dev zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN tools/ephsrv-fork.sh /swisseph \
 && make -f Makefile.ephsrv SWE_HOME=/swisseph -j4 astrolog-ephd

FROM debian:12-slim
# curl only for the HEALTHCHECK below.
RUN apt-get update && apt-get install -y --no-install-recommends \
      libssl3 zlib1g ca-certificates curl \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --no-create-home --shell /usr/sbin/nologin ephd
COPY --from=build /src/astrolog-ephd /usr/local/bin/astrolog-ephd
USER ephd
EXPOSE 47190
STOPSIGNAL SIGTERM
# Plain or TLS, whichever the server was started with, on the default port;
# a container started with another --port needs its own HEALTHCHECK.
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s \
  CMD curl -fsS http://127.0.0.1:47190/healthz \
   || curl -fsSk https://127.0.0.1:47190/healthz || exit 1
ENTRYPOINT ["astrolog-ephd"]
CMD ["--port", "47190", "--ephe", "/ephe"]
