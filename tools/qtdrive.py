#!/usr/bin/env python3
"""
Drive the Qt build's UI by widget name instead of by pixel coordinate.

Qt draws its widgets inside one X window, so xwininfo and xdotool can only
see the window, not the buttons in it. That is why driving a dialog by hand
means measuring a screenshot, and why a mis-measured click leaves the dialog
open while a before/after comparison happily reports "identical" -- nothing
happened, and nothing said so.

Qt does expose every widget over AT-SPI, so a button can be found by its
label and clicked at the coordinates it reports itself. This wraps that.

Everything runs on a private Xvfb display and a private D-Bus session, so
it touches neither the user's screen nor their desktop's accessibility
settings. Qt only builds the tree when ScreenReaderEnabled is true, which
is set on the private bus here.

Usage:

  tools/qtdrive.sh tree                       # dump the widget tree
  tools/qtdrive.sh tree --args "-i my.as"     # ... with the app given args
  tools/qtdrive.sh run SCRIPT                 # run a scenario, see below

A scenario is one command per line; "#" comments and blank lines ignored:

  menu Setting Object Selectio&ns...   open a menu bar item, then an entry
  click OK                             click a widget by name
  type combo-box#3 Nessus              set a field via accessibility
  typeinto combo-box#3 7066            click it and type, for combos
  expect-window Object Selections      fail unless that window exists
  expect-no-window Object Selections   fail unless it does not
  expect-value combo-box#3 Nessus      fail unless the field reads that
  shot out/x.png                       screenshot the whole display
  tree                                 dump the tree at this point
  sleep 1
  key ctrl+l                           raw keystroke to the focus window --
                                       the escape hatch for native (GTK)
                                       dialogs AT-SPI cannot see into
  typeraw /tmp/x.as                    raw typing, same escape hatch

Exit status is 0 only if every expect-* passed.
"""

import os
import subprocess
import sys
import time

DISPLAY = os.environ.get("QTDRIVE_DISPLAY", ":90")
APP = os.environ.get("QTDRIVE_APP", "./astrolog-qt")


# ---------------------------------------------------------------- tree

def _desktop():
    import pyatspi
    return pyatspi.Registry.getDesktop(0)


def app_root(name_hint="astrolog"):
    """The application's accessibility root, or None if it hasn't appeared.

    Qt registers the process before it has any windows, so an entry with no
    children is normal for a moment and is not the app being ready."""
    for app in _desktop():
        try:
            if name_hint in (app.name or "") and app.childCount > 0:
                return app
        except Exception:
            continue
    return None


def wait_for_app(timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        a = app_root()
        if a is not None:
            return a
        time.sleep(0.3)
    return None


def walk(node, depth=0, limit=4000):
    """Yield (depth, node) for the whole subtree, breadth-limited."""
    out = [(depth, node)]
    if len(out) > limit:
        return out
    try:
        for child in node:
            if child is None:
                continue
            out.extend(walk(child, depth + 1, limit))
            if len(out) > limit:
                break
    except Exception:
        pass
    return out


def dump_tree(node, out=sys.stdout):
    for depth, n in walk(node):
        try:
            name, role = n.name, n.getRoleName()
        except Exception:
            continue
        ext = extents(n)
        where = " @%d,%d %dx%d" % ext if ext else ""
        out.write("%s%-24s %s%s\n" % ("  " * depth, repr(name), role, where))


def extents(node):
    """Screen coordinates of a widget, or None if it has no position."""
    try:
        import pyatspi
        c = node.queryComponent()
        e = c.getExtents(pyatspi.DESKTOP_COORDS)
        if e.width <= 0 or e.height <= 0:
            return None
        return (e.x, e.y, e.width, e.height)
    except Exception:
        return None


def find(root, name, role=None):
    """Locate a widget.

    By name, ignoring an "&" mnemonic marker -- which is how buttons, menu
    entries and labels are reached, and is what you want almost always.

    Or positionally, as "role#n" (1-based), for the case names cannot
    cover: a grid of identical rows. Astrolog's dialogs are built from
    astrolog.rc, so a field's *symbol* there (deOs01) is not its accessible
    name -- Qt reports a field's current value instead. So the fifth combo
    in a dialog is "combo-box#5", not "dcOs05" -- hyphens because
    scenario lines are split on whitespace.
    """
    if "#" in (name or ""):
        want_role, _, idx = name.rpartition("#")
        # Roles have spaces ("combo box"); scenarios are whitespace split,
        # so they are written with hyphens ("combo-box#5").
        want_role = want_role.replace("-", " ")
        try:
            idx = int(idx)
        except ValueError:
            # Not positional after all -- a real label containing '#'
            # ('Open Chart #2...'). Fall through to name matching.
            idx = None
        if idx is not None:
            seen = 0
            for _, n in walk(root):
                try:
                    if n.getRoleName() != want_role:
                        continue
                    # A combo box contains its own "text" child, so
                    # counting every text would make text#1 the first
                    # combo's editor rather than the first standalone
                    # field. Count only widgets that are not part of
                    # another control.
                    par = n.parent
                    if par is not None and par.getRoleName() in (
                            "combo box", "spin button", "scroll bar"):
                        continue
                except Exception:
                    continue
                seen += 1
                if seen == idx:
                    return n
            return None

    # A menu item's accessible name carries its shortcut column after a
    # tab ('Open Chart...\tAlt+o') once a QAction has a shortcut -- which
    # nearly every item does since the accelerator work. Match on the
    # label half only, or every shortcut-bearing item is unreachable.
    want = (name or "").split("\t")[0].replace("&", "")
    for _, n in walk(root):
        try:
            if (n.name or "").split("\t")[0].replace("&", "") != want:
                continue
            if role is not None and n.getRoleName() != role:
                continue
            return n
        except Exception:
            continue
    return None


def find_window(root, title):
    for _, n in walk(root):
        try:
            if n.getRoleName() in ("frame", "dialog", "alert") and \
               (n.name or "") == title:
                return n
        except Exception:
            continue
    return None


# ---------------------------------------------------------------- acting

def xdo(*args):
    env = dict(os.environ, DISPLAY=DISPLAY)
    return subprocess.run(["xdotool"] + list(args), env=env,
                          capture_output=True, text=True)


def click_node(node):
    """Click a widget at the middle of where it says it is.

    Prefers the accessible action, which needs no pointer at all; falls back
    to a real click for widgets that expose no action."""
    try:
        act = node.queryAction()
        for i in range(act.nActions):
            if act.getName(i) in ("click", "press", "activate"):
                act.doAction(i)
                return True
    except Exception:
        pass
    ext = extents(node)
    if not ext:
        return False
    x, y, w, h = ext
    xdo("mousemove", str(x + w // 2), str(y + h // 2), "click", "1")
    return True


def set_text(node, value):
    try:
        node.queryEditableText().setTextContents(value)
        return True
    except Exception:
        return False


def get_text(node):
    try:
        t = node.queryText()
        return t.getText(0, t.characterCount)
    except Exception:
        return node.name


# ---------------------------------------------------------------- session

class Session(object):
    def __init__(self, args=""):
        self.args = args
        self.proc = None

    def __enter__(self):
        cmd = [APP] + (self.args.split() if self.args else [])
        self.proc = subprocess.Popen(
            cmd,
            env=dict(os.environ, DISPLAY=DISPLAY, QT_ACCESSIBILITY="1",
                     QT_LINUX_ACCESSIBILITY_ALWAYS_ON="1"),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.root = wait_for_app()
        if self.root is None:
            raise SystemExit("app never appeared on the accessibility bus")
        return self

    def __exit__(self, *exc):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()


def open_menu(root, top, item):
    """Open a menu bar item and pick an entry, both by label.

    Done through the accessible actions rather than alt+mnemonic: sending
    the second key too early silently does nothing, which is a whole class
    of "the test flaked" that simply cannot happen here."""
    # Menu bar entries report as "menu item", the same role as the entries
    # inside them; the role is not what distinguishes them.
    bar = find(root, top, "menu item")
    if bar is None:
        bar = find(root, top)
    if bar is None:
        return "no menu %r" % top
    click_node(bar)
    time.sleep(0.4)
    entry = find(root, item)
    if entry is None:
        return "no menu item %r" % item
    click_node(entry)
    time.sleep(0.6)
    return None


def run_script(path, args=""):
    fails = []
    with Session(args) as s:
        for raw in open(path):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 1)
            cmd = parts[0]
            rest = parts[1] if len(parts) > 1 else ""
            root = app_root() or s.root

            if cmd == "menu":
                top, item = rest.split(None, 1)
                err = open_menu(root, top, item)
                if err:
                    fails.append("menu: " + err)
            elif cmd == "click":
                n = find(root, rest)
                if n is None or not click_node(n):
                    fails.append("click: no widget %r" % rest)
                time.sleep(0.5)
            elif cmd == "type":
                wid, value = rest.split(None, 1)
                n = find(root, wid)
                if n is None or not set_text(n, value):
                    fails.append("type: no editable widget %r" % wid)
            elif cmd == "typeinto":
                # A QComboBox does not expose EditableText itself -- its
                # internal line edit does -- so "type" cannot reach one.
                # Click it and use the keyboard, which is also closer to
                # what a user actually does.
                wid, value = rest.split(None, 1)
                n = find(root, wid)
                ext = extents(n) if n is not None else None
                if not ext:
                    fails.append("typeinto: no widget %r" % wid)
                else:
                    x, y, w, h = ext
                    xdo("mousemove", str(x + w // 3), str(y + h // 2),
                        "click", "1")
                    time.sleep(0.3)
                    xdo("key", "--clearmodifiers", "ctrl+a")
                    time.sleep(0.2)
                    xdo("type", "--delay", "40", "--", value)
                    time.sleep(0.4)
            elif cmd == "expect-window":
                if find_window(root, rest) is None:
                    fails.append("expect-window: %r is not open" % rest)
            elif cmd == "expect-no-window":
                if find_window(root, rest) is not None:
                    fails.append("expect-no-window: %r is still open" % rest)
            elif cmd == "expect-value":
                wid, value = rest.split(None, 1)
                n = find(root, wid)
                got = get_text(n) if n is not None else None
                if got != value:
                    fails.append("expect-value: %s is %r, wanted %r"
                                 % (wid, got, value))
            elif cmd == "shot":
                os.makedirs(os.path.dirname(rest) or ".", exist_ok=True)
                subprocess.run(["import", "-window", "root", rest],
                               env=dict(os.environ, DISPLAY=DISPLAY))
            elif cmd == "tree":
                dump_tree(root)
            elif cmd == "key":
                # Raw keystrokes to whatever has focus -- the escape hatch
                # for native (GTK) dialogs that AT-SPI cannot see into.
                xdo("key", "--clearmodifiers", rest)
                time.sleep(0.3)
            elif cmd == "typeraw":
                xdo("type", "--delay", "40", "--", rest)
                time.sleep(0.3)
            elif cmd == "sleep":
                time.sleep(float(rest))
            else:
                fails.append("unknown command %r" % cmd)
            print("  %-60s %s" % (line, "" if not fails or
                                  fails[-1].split(":")[0] != cmd else "FAIL"))
            # A crash must never be silent: without this every later
            # command just fails to find a widget and the real event --
            # the process dying -- is buried.
            rc = s.proc.poll()
            if rc is not None:
                fails.append("the app exited (status %d) after %r" % (rc, line))
                break
    for f in fails:
        print("FAIL  " + f)
    print("%s: %d command(s) failed" % ("FAIL" if fails else "PASS", len(fails)))
    return 1 if fails else 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mode = sys.argv[1]
    args = ""
    if "--args" in sys.argv:
        args = sys.argv[sys.argv.index("--args") + 1]
    if mode == "tree":
        with Session(args) as s:
            dump_tree(app_root() or s.root)
        return 0
    if mode == "run":
        return run_script(sys.argv[2], args)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
