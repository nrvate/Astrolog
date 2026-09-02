#!/bin/sh
# Build signed apt and dnf repositories from a directory of packages.
#
#   tools/make-repo.sh <package-dir> <output-dir> [base-url]
#
# QT_CI_PLAN.md item 4.7. Release assets make a user download a file and
# install it by hand, once per release. A repository makes it
# "apt install astrolog" and then "apt upgrade" forever, which is most of
# what packaging is for.
#
# LAYOUT, and both halves are flat on purpose -- there is one component
# and a handful of packages, so a pool hierarchy would be ceremony:
#
#   apt/dists/stable/Release{,.gpg}  InRelease
#   apt/dists/stable/main/binary-amd64/Packages{,.gz}
#   apt/pool/main/*.deb
#   rpm/repodata/repomd.xml{,.asc}
#   rpm/*.rpm
#   astrolog.gpg      the public key, BINARY, for apt signed-by=
#   astrolog.asc      the same key ARMOURED, for dnf gpgkey=
#
# ONE REPOSITORY FOR EVERY DISTRIBUTION, which is safe here because the
# codename is in each package's version: apt sees 8.00+qt.1~jammy and
# 8.00+qt.1~noble as different versions of one package and picks the
# highest, which is the newest release built for the newest distribution
# -- not necessarily the one that will install. That is the known cost of
# a single-suite repository and the reason the README tells a 22.04 user
# to pin. Splitting into per-codename suites is the fix if it ever bites.
#
# SIGNING is optional here and required in CI. Without a key this builds
# an unsigned repository, which is useful for testing and useless to
# publish: apt refuses it and dnf has to be told to skip gpgcheck, which
# is a bad habit to teach. The workflow passes a key; a local run usually
# will not.
set -eu

pkgdir=${1:?usage: make-repo.sh <package-dir> <output-dir> [base-url]}
out=${2:?usage: make-repo.sh <package-dir> <output-dir> [base-url]}
baseurl=${3:-}

command -v apt-ftparchive >/dev/null || { echo "need apt-ftparchive (apt-utils)"; exit 1; }
command -v createrepo_c >/dev/null || { echo "need createrepo_c"; exit 1; }

pkgdir=$(cd "$pkgdir" && pwd)
rm -rf "$out"
mkdir -p "$out/apt/pool/main" "$out/apt/dists/stable/main/binary-amd64" "$out/rpm"

ndeb=$(find "$pkgdir" -name '*.deb' | wc -l)
nrpm=$(find "$pkgdir" -name '*.rpm' | wc -l)
[ "$ndeb" -gt 0 ] || { echo "no .deb in $pkgdir -- refusing to publish an empty repository"; exit 1; }
[ "$nrpm" -gt 0 ] || { echo "no .rpm in $pkgdir -- refusing to publish an empty repository"; exit 1; }
find "$pkgdir" -name '*.deb' -exec cp {} "$out/apt/pool/main/" \;
find "$pkgdir" -name '*.rpm' -exec cp {} "$out/rpm/" \;

# --- apt ---
( cd "$out/apt"
  apt-ftparchive packages pool/main > dists/stable/main/binary-amd64/Packages
  gzip -9kf dists/stable/main/binary-amd64/Packages
  apt-ftparchive \
    -o APT::FTPArchive::Release::Origin=Astrolog \
    -o APT::FTPArchive::Release::Label=Astrolog \
    -o APT::FTPArchive::Release::Suite=stable \
    -o APT::FTPArchive::Release::Codename=stable \
    -o APT::FTPArchive::Release::Architectures=amd64 \
    -o APT::FTPArchive::Release::Components=main \
    release dists/stable > dists/stable/Release )

# --- dnf ---
# The PACKAGES are signed, not just the metadata. dnf's gpgcheck=1 checks
# each package's own signature; repo_gpgcheck=1 checks repomd.xml. Sign
# only the metadata and a repo advertising gpgcheck=1 fails on the first
# install with "package is not signed", which is a worse first impression
# than no repository. Signing happens before createrepo_c, because it
# rewrites the .rpm and would otherwise invalidate the checksums in the
# metadata.
if [ -n "${GPG_KEY_ID:-}" ] && command -v rpmsign >/dev/null; then
  cat > "$HOME/.rpmmacros" <<MACROS
%_gpg_name $GPG_KEY_ID
%__gpg_sign_cmd %{__gpg} gpg --batch --pinentry-mode loopback --no-verbose --no-armor --no-secmem-warning -u "%{_gpg_name}" -sbo %{__signature_filename} %{__plaintext_filename}
MACROS
  rpmsign --addsign "$out"/rpm/*.rpm >/dev/null
  echo "rpm packages signed"
elif [ -n "${GPG_KEY_ID:-}" ]; then
  echo "NO rpmsign: the .rpm files are unsigned, so a repo advertising"
  echo "gpgcheck=1 will refuse them. Install rpm-sign (Fedora/EL) or"
  echo "rpm (Debian) before building a repository to publish."
  exit 1
fi
createrepo_c --quiet "$out/rpm"

# --- signing ---
if [ -n "${GPG_KEY_ID:-}" ]; then
  gpg --batch --yes --armor --detach-sign -u "$GPG_KEY_ID" \
      -o "$out/apt/dists/stable/Release.gpg" "$out/apt/dists/stable/Release"
  gpg --batch --yes --clearsign -u "$GPG_KEY_ID" \
      -o "$out/apt/dists/stable/InRelease" "$out/apt/dists/stable/Release"
  gpg --batch --yes --armor --detach-sign -u "$GPG_KEY_ID" \
      -o "$out/rpm/repodata/repomd.xml.asc" "$out/rpm/repodata/repomd.xml"
  # TWO EXPORTS, because the two package managers want different formats
  # and the extension is not decoration. apt's "signed-by=" reads a
  # BINARY keyring when the file is named .gpg; hand it an armoured key
  # and it says "NO_PUBKEY ... is not signed", which is what the first
  # version of this script did. dnf's gpgkey= wants the ARMOURED form.
  gpg --batch --yes --export "$GPG_KEY_ID" > "$out/astrolog.gpg"
  gpg --batch --yes --armor --export "$GPG_KEY_ID" > "$out/astrolog.asc"
  echo "signed with $GPG_KEY_ID"
else
  echo "UNSIGNED: no GPG_KEY_ID. Fine for a local check, not for publishing --"
  echo "apt refuses an unsigned repository and dnf needs gpgcheck=0, which is"
  echo "a bad habit to teach."
fi

# The instructions, generated so they cannot drift from the layout above.
if [ -n "$baseurl" ]; then
  cat > "$out/index.html" <<HTML
<!doctype html><meta charset=utf-8><title>Astrolog package repository</title>
<style>body{font:14px/1.5 system-ui,sans-serif;max-width:46em;margin:3em auto;padding:0 1em}
pre{background:#f4f4f4;padding:1em;overflow-x:auto}code{background:#f4f4f4;padding:.1em .3em}</style>
<h1>Astrolog package repository</h1>
<p>The Qt/Linux port of Astrolog 8.00. Packages are built on the
distribution they target and installed into a clean container before
publishing.</p>
<h2>Debian / Ubuntu</h2>
<pre>sudo curl -fsSL -o /usr/share/keyrings/astrolog.gpg $baseurl/astrolog.gpg
echo "deb [signed-by=/usr/share/keyrings/astrolog.gpg] $baseurl/apt stable main" \\
  | sudo tee /etc/apt/sources.list.d/astrolog.list
sudo apt update &amp;&amp; sudo apt install astrolog</pre>
<h2>Fedora / RHEL / Rocky / Alma</h2>
<pre>sudo tee /etc/yum.repos.d/astrolog.repo &lt;&lt;'EOF'
[astrolog]
name=Astrolog
baseurl=$baseurl/rpm
enabled=1
gpgcheck=1
gpgkey=$baseurl/astrolog.asc
EOF
sudo dnf install astrolog</pre>
<p>One suite serves every distribution, and each package's version carries
its codename, so apt offers the highest version it can see rather than the
one built for your release. Pin if that matters:
<code>apt install astrolog=8.00+qt.1~jammy</code>.</p>
HTML
fi

echo "repo built: $ndeb deb, $nrpm rpm, in $out"
