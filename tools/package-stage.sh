#!/bin/sh
# Stage a Linux install tree, ready for dpkg-deb or rpmbuild.
#
#   tools/package-stage.sh <destdir> [prefix]
#
# QT_CI_PLAN.md item 4.2. Q1 is answered: native packages, .deb and .rpm,
# rather than an AppImage or a bare tarball. An AppImage carries ~70 MB of
# Qt closure for a 2.5 MB program; a tarball needs the user to install Qt
# themselves, which is most of what a release is supposed to save them.
# A native package lets the distribution's own resolver do it.
#
# THE LAYOUT IS NOT FHS-OBVIOUS, AND THE REASON IS IN THE PROGRAM.
# Astrolog resolves its data -- ephemeris, atlas, fonts, settings -- from
# THE DIRECTORY OF ITS OWN EXECUTABLE. Item 7.2b measured what happens
# otherwise: a binary moved elsewhere silently reads a different ephemeris
# and answers with Moshier positions that look like a numerical
# regression. So the binaries do not go in /usr/bin with their data in
# /usr/share; the whole payload goes to $LIBDIR/astrolog and /usr/bin gets
# a wrapper that execs it.
#
# That is exactly what "make install" already does, and it is the tested
# mechanism rather than a new one. Symlinks would probably work for the Qt
# build, whose applicationDirPath() resolves through /proc/self/exe, but
# "probably" is not a packaging decision.
set -eu

dest=${1:?usage: package-stage.sh <destdir> [prefix]}
prefix=${2:-/usr}
cd "$(dirname "$0")/.."

[ -x ./astrolog ] || { echo "build it first: make"; exit 1; }
[ -x ./astrolog-qt ] || { echo "build it first: make qt"; exit 1; }

libdir="$dest$prefix/lib/astrolog"
bindir="$dest$prefix/bin"
appdir="$dest$prefix/share/applications"
icondir="$dest$prefix/share/icons/hicolor"
docdir="$dest$prefix/share/doc/astrolog"

rm -rf "$dest"
mkdir -p "$libdir" "$bindir" "$appdir" "$docdir"

# The program and everything it reads at runtime, together.
cp astrolog astrolog-qt "$libdir/"
cp -r ephem font "$libdir/"
cp astrolog.as atlas.as timezone.as "$libdir/"
cp sefstars.txt seorbel.txt astexo.csv earth.bmp "$libdir/"
chmod 0755 "$libdir/astrolog" "$libdir/astrolog-qt"

# nrvate.as must never ship: it is the maintainer's personal settings and
# points -Yi1 at a NAS mount that exists on one machine in the world.
[ ! -e "$libdir/nrvate.as" ] || { echo "nrvate.as staged -- refusing"; exit 1; }

for b in astrolog astrolog-qt; do
  cat > "$bindir/$b" <<EOF
#!/bin/sh
# Astrolog reads its data files from the directory of its own executable,
# so the real binary stays beside them in $prefix/lib/astrolog and this
# runs it there. See tools/package-stage.sh.
exec "$prefix/lib/astrolog/$b" "\$@"
EOF
  chmod 0755 "$bindir/$b"
done

cat > "$appdir/astrolog.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Astrolog
GenericName=Astrology Chart Calculator
Comment=Cast and display astrological charts
Exec=$prefix/bin/astrolog-qt
Icon=astrolog
Terminal=false
Categories=Science;Astronomy;
Keywords=astrology;horoscope;chart;ephemeris;zodiac;
StartupNotify=true
EOF

for s in 16 32 48 64 128 256; do
  [ -f "icons/astrolog$s.png" ] || continue
  mkdir -p "$icondir/${s}x${s}/apps"
  cp "icons/astrolog$s.png" "$icondir/${s}x${s}/apps/astrolog.png"
done

cp license.htm "$docdir/copyright.htm"
cp astrolog.htm changes.htm "$docdir/"

echo "staged $(find "$dest" -type f | wc -l) files into $dest"
