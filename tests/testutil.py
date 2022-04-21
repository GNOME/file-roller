from gi.repository import GLib, Gio

print('foo')
from dogtail.utils import isA11yEnabled, enableA11y

if not isA11yEnabled():
    enableA11y(True)

from contextlib import AbstractContextManager, contextmanager
print('foobat')
from dogtail.predicate import Predicate
from enum import Enum
import errno
import os
from pathlib import Path
import signal
import subprocess
from tempfile import TemporaryDirectory
from textwrap import dedent
import time
from typing import Tuple

import dbus
import dbusmock
from dbus.mainloop.glib import DBusGMainLoop


@contextmanager
def bus_for_testing(system_bus: bool = False) -> dbus.Bus:
    try:
        case = dbusmock.DBusTestCase()
        case.start_session_bus()
        DBusGMainLoop(set_as_default=True)
        dbus.bus.BusConnection(os.environ['DBUS_SESSION_BUS_ADDRESS'])
        yield case, case.get_dbus(system_bus=system_bus)
    finally:
        case.tearDown()


@contextmanager
def mocked_portal_open_uri(mock_case: dbusmock.DBusTestCase, bus: dbus.Bus):
    p_mock = mock_case.spawn_server(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.OpenURI",
        # stdout=subprocess.PIPE,
    )

    try:
        # Get a proxy for the Desktop object's Mock interface
        desktop_portal_mock = dbus.Interface(
            bus.get_object(
                "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop"
            ),
            dbusmock.MOCK_IFACE,
        )

        yield desktop_portal_mock

    finally:
        # p_mock.stdout.close()
        p_mock.terminate()
        p_mock.wait()

    def test_suspend_on_idle(self):
        # run your program in a way that should trigger one suspend call

        # now check the log that we got one OpenFile() call
        self.assertRegex(self.p_mock.stdout.readline(), b"^[0-9.]+ Suspend$")


def start_dbus_session() -> Tuple[int, str]:
    """Start a D-Bus daemon
    Return (pid, address) pair.
    Normally you do not need to call this directly. Use start_system_bus()
    and start_session_bus() instead.
    """
    argv = [
        "dbus-daemon",
        "--fork",
        "--print-address=1",
        "--print-pid=1",
    ]

    if "DBUS_SESSION_CONFIG" in os.environ:
        argv.append("--config-file=" + os.environ["DBUS_SESSION_CONFIG"])
    else:
        argv.append("--session")
    lines = subprocess.check_output(argv, universal_newlines=True).strip().splitlines()
    assert len(lines) == 2, "expected exactly 2 lines of output from dbus-daemon"
    # usually the first line is the address, but be lenient and accept any order
    try:
        return (int(lines[1]), lines[0])
    except ValueError:
        return (int(lines[0]), lines[1])


def stop_dbus(pid: int) -> None:
    """Stop a D-Bus daemon
    Normally you do not need to call this directly. When you use
    start_system_bus() and start_session_bus(), these buses are
    automatically stopped in tearDownClass().
    """
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    for _ in range(50):
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError as e:
            if e.errno == errno.ESRCH:
                break
            raise
        time.sleep(0.1)
    else:
        sys.stderr.write("ERROR: timed out waiting for bus process to terminate\n")
        os.kill(pid, signal.SIGKILL)
        time.sleep(0.5)
    signal.signal(signal.SIGTERM, signal.SIG_DFL)


# @contextmanager
# def session_bus_for_testing() -> AbstractContextManager[Gio.DBusConnection]:
#     original_bus_address = os.environ.get("DBUS_SESSION_BUS_ADDRESS", None)

#     pid, bus_address = start_dbus_session()
#     print(pid, bus_address, original_bus_address)
#     os.environ["DBUS_SESSION_BUS_ADDRESS"] = bus_address

#     try:
#         # bus = Gio.bus_get_sync(Gio.BusType.SESSION)
#         bus = Gio.DBusConnection.new_for_address_sync(bus_address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION)

#         yield bus
#     finally:
#         stop_dbus(pid)

#         if original_bus_address is not None:
#             os.environ["DBUS_SESSION_BUS_ADDRESS"] = original_bus_address
#         else:
#             os.environ.pop("DBUS_SESSION_BUS_ADDRESS")


# @contextmanager
# def mocked_portal_open_uri(bus: Gio.DBusConnection) -> AbstractContextManager[Gio.DBusConnection]:
#     with open(Path(__file__).parent / "org.freedesktop.portal.OpenURI.xml") as introspection_xml:
#         introspection_data = Gio.DBusNodeInfo.new_for_xml(introspection_xml.read())
#         assert introspection_data is not None

#         def on_bus_lost():
#             print("on_bus_lost")

#         def on_bus_acquired():
#             print("on_bus_acquired")
#             registration_id = bus.register_object(
#                 object_path="/org/freedesktop/portal/desktop",
#                 interface_info=introspection_data.interfaces[0],
#                 method_call_closure=on_method_called,
#             )
#             assert registration_id > 0

#         def on_method_called():
#             print("on_method_called")
#             pass

#         try:
#             owner_id = Gio.bus_own_name_on_connection(
#                 connection=bus,
#                 name="org.freedesktop.portal.Desktop",
#                 flags=Gio.BusNameOwnerFlags.REPLACE | Gio.BusNameOwnerFlags.DO_NOT_QUEUE,
#                 name_acquired_closure=on_bus_acquired,
#                 name_lost_closure=on_bus_lost,
#             )
#             print(2)

#             print(3)
#             yield owner_id

#         finally:
#             print('unownd')
#             Gio.bus_unown_name(owner_id)


class WaitState(Enum):
    RUNNING = 1  # waiting to see the service
    SUCCESS = 2  # seen it successfully
    TIMEOUT = 3  # timed out before seeing it


def wait_for_name(bus: Gio.DBusConnection, name: str, timeout: int = 100) -> bool:
    wait_state = WaitState.RUNNING
    print(name)

    def wait_name_appeared_cb(connection: Gio.DBusConnection, name: str, name_owner: str):
        nonlocal wait_state
        print('wait_name_appeared_cb')
        wait_state = WaitState.SUCCESS

    def wait_timeout_cb():
        nonlocal wait_state
        print('wait_timeout_cb')
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

        # Importing it has side-effects.
        from dogtail import tree
        self.tree = tree

    def get_application(self):
        return self.tree.root.application(self.app_name)

    def get_window(self, name: str) -> self.tree.Window:
        # Cannot use default window finder because the window name has extra space after the file name.
        return self.get_application().findChild(IsAWindowApproximatelyNamed(name), False)


@contextmanager
def new_user_config_dir() -> AbstractContextManager[Path]:
    with TemporaryDirectory() as confdir:
        os.environ["XDG_CONFIG_HOME"] = confdir
        yield Path(confdir)


@contextmanager
def new_user_data_dir() -> AbstractContextManager[Path]:
    with TemporaryDirectory() as datadir:
        os.environ["XDG_DATA_HOME"] = datadir
        yield Path(datadir)


@contextmanager
def test_file_roller():
    # Use custom user config and data directories to avoid changing user’s MIME associations.
    with bus_for_testing() as (mock_case, legacy_bus), \
        new_user_config_dir() as confdir, \
        new_user_data_dir() as datadir:

        # Redirect non-portal based gtk_show_uri_on_window to portal so that we can capture it by the mocked portal.
        desktop_file_path = datadir / "applications" / "dummy-editor.desktop"
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

        # Start the tested app.
        bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        dbus_app = FileRollerDbus(bus)

        try:
            assert wait_for_name(bus, dbus_app.APPLICATION_ID), f"Waiting for {dbus_app.APPLICATION_ID} should not time out."
            print(1)

            dogtail_app = FileRollerDogtail("file-roller")
            print(1)

            yield bus, (mock_case, legacy_bus), dbus_app, dogtail_app
        finally:
            dbus_app.quit()
