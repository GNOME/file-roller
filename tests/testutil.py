from gi.repository import GLib, Gio

from dogtail.utils import isA11yEnabled, enableA11y

if not isA11yEnabled():
    enableA11y(True)

from contextlib import contextmanager
from enum import Enum
from dogtail import tree
from dogtail.predicate import Predicate
from pathlib import Path
from tempfile import TemporaryDirectory
from textwrap import dedent

import os
import subprocess


class WaitState(Enum):
    RUNNING = 1  # waiting to see the service
    SUCCESS = 2  # seen it successfully
    TIMEOUT = 3  # timed out before seeing it


def wait_for_name(bus: Gio.DBusConnection, name: str, timeout: int = 10) -> bool:
    wait_state = WaitState.RUNNING

    def wait_name_appeared_cb(connection: Gio.DBusConnection, name: str, name_owner: str):
        nonlocal wait_state
        wait_state = WaitState.SUCCESS

    def wait_timeout_cb():
        nonlocal wait_state
        wait_state = WaitState.TIMEOUT

    watch_id = Gio.bus_watch_name_on_connection(bus, name, Gio.BusNameWatcherFlags.NONE, wait_name_appeared_cb, None)

    timer_id = GLib.timeout_add_seconds(interval=timeout, function=wait_timeout_cb)

    ctx = GLib.main_context_default()
    while wait_state == WaitState.RUNNING:
        ctx.iteration(True)

    Gio.bus_unwatch_name(watch_id)
    GLib.source_remove(timer_id)

    return wait_state == WaitState.SUCCESS


class IsAWindowApproximatelyNamed(Predicate):
    """Predicate subclass that looks for a top-level window by name substring"""

    def __init__(self, windowNameInfix):
        self.windowNameInfix = windowNameInfix

    def satisfiedByNode(self, node):
        return node.roleName == "frame" and self.windowNameInfix in node.name

    def describeSearchResult(self):
        return f"window with “{self.windowNameInfix}” in name"


class FileRollerDbus:
    APPLICATION_ID = "org.gnome.FileRoller"

    def __init__(self, bus: Gio.DBusConnection):
        self._bus = bus
        builddir = os.environ.get("G_TEST_BUILDDIR", None)
        if builddir and not "TESTUTIL_DONT_START" in os.environ:
            subprocess.Popen(
                [os.path.join(builddir, "..", "src", "file-roller")],
                cwd=os.path.join(builddir, ".."),
            )
        else:
            self._do_bus_call("Activate", GLib.Variant("(a{sv})", ([],)))

    def _do_bus_call(self, method: str, params: GLib.Variant) -> None:
        self._bus.call_sync(
            self.APPLICATION_ID,
            "/" + self.APPLICATION_ID.replace(".", "/"),
            "org.freedesktop.Application",
            method,
            params,
            None,
            Gio.DBusCallFlags.NONE,
            -1,
            None,
        )

    def open_file(self, path: Path) -> None:
        self._do_bus_call(
            "ActivateAction",
            GLib.Variant(
                "(sava{sv})",
                (
                    "open-archive",
                    [
                        GLib.Variant("s", str(path)),
                    ],
                    [],
                ),
            ),
        )

    def quit(self) -> None:
        self._do_bus_call(
            "ActivateAction", GLib.Variant("(sava{sv})", ("quit", [], []))
        )


class FileRollerDogtail:
    def __init__(self, app_name):
        self.app_name = app_name

    def get_application(self) -> tree.Application:
        return tree.root.application(self.app_name)

    def get_window(self, name: str) -> tree.Window:
        # Cannot use default window finder because the window name has extra space after the file name.
        return self.get_application().findChild(
            IsAWindowApproximatelyNamed(name), False
        )


@contextmanager
def test_file_roller():
    try:
        dbus_app = None
        _bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        with TemporaryDirectory() as confdir, TemporaryDirectory() as datadir:
            os.environ["XDG_CONFIG_HOME"] = confdir
            os.environ["XDG_DATA_HOME"] = datadir

            desktop_file_path = Path(datadir) / "applications" / "dummy-editor.desktop"
            desktop_file_path.parent.mkdir(parents=True, exist_ok=True)
            with open(desktop_file_path, "w") as desktop_file:
                desktop_file.write(
                    dedent(
                        """[Desktop Entry]
                        Exec=sh -c 'exec 3< "$1"; gdbus call --session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop --method org.freedesktop.portal.OpenURI.OpenFile --timeout 5 0 "3" "{}"' portal-open %u
                        MimeType=text/plain;
                        Terminal=false
                        Type=Application
                        """
                    )
                )

            desktop = Gio.DesktopAppInfo.new_from_filename(str(desktop_file_path))
            desktop.set_as_default_for_type("text/plain")

            dbus_app = FileRollerDbus(_bus)
            assert wait_for_name(_bus, dbus_app.APPLICATION_ID), f"Waiting for {dbus_app.APPLICATION_ID} should not time out."
            dogtail_app = FileRollerDogtail("file-roller")
            yield dbus_app, dogtail_app
    finally:
        if dbus_app:
            dbus_app.quit()
