#!/bin/sh
# Print the "Installing" section that heads every release's notes.
#
#   tools/release-notes.sh > notes.md
#
# The text is tools/release-notes.md, which is user-facing prose -- the
# Gatekeeper and SmartScreen instructions, what each artifact is for --
# and until 2026-09-04 it lived inside release.yml as a shell heredoc,
# with every backtick escaped, editable only through a workflow diff and
# readable nowhere else. Now it is a Markdown file anyone can read and
# fix, and this fills in the two things that change without anyone
# editing it: the Ubuntu and Fedora releases the matrix builds, from
# tools/distros.json.
#
# release.yml prepends the result to GitHub's generated commit list
# (--notes-file plus --generate-notes).
set -eu
cd "$(dirname "$0")/.."

ubuntus=$(python3 tools/distros.py deb | grep -o 'ubuntu-[0-9.]*' | sed 's/ubuntu-//' | paste -sd/)
fedoras=$(python3 tools/distros.py rpm | grep -o 'fedora-[0-9]*' | sed 's/fedora-//' | paste -sd/)
[ -n "$ubuntus" ] && [ -n "$fedoras" ] || {
  echo "release-notes: could not read the distribution list" >&2; exit 1; }

sed -e "s|@UBUNTUS@|$ubuntus|g" -e "s|@FEDORAS@|$fedoras|g" tools/release-notes.md
