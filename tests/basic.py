#!/usr/bin/env python3

from gi.repository import Gio, GLib
from pathlib import Path
from testutil import test_file_roller, mocked_portal_open_uri, mocked_portal_open_uri_2
from time import sleep


# with test_file_roller() as (bus, (mock_case, legacy_bus), app, app_tree), \
#     mocked_portal_open_uri(mock_case, legacy_bus) as open_uri_portal:

with test_file_roller() as (bus, app, app_tree), \
    mocked_portal_open_uri_2(bus) as open_uri_portal:

    # open_uri_portal.AddMethod(
    #     interface = "",
    #     name = "OpenFile",
    #     in_sig = "sha{sv}",
    #     out_sig = "o",
    #     code = "print(args)",
    # )

    app.open_file(Path(__file__).parent / "data" / "texts.tar.gz")

    win = app_tree.get_window("texts.tar.gz")

    file_listing = win.child(roleName="table")
    assert (
        file_listing.get_n_rows() == 1
    ), "The archive should contain single directory in top-level"

    texts_directory = file_listing.child("texts")
    texts_directory.doubleClick()

    assert (
        file_listing.get_n_rows() == 2
    ), "The archive should contain two files inside texts/ directory"

    hello_txt = file_listing.child("hello.txt")
    hello_txt.doubleClick()

    # TODO: wait for the mocked portal to give us extracted file path

    # TODO: Edit the file

    # TODO: Check that File-Roller asks us to persist the changes.

    # TODO: Check that the archive contents get updated.

    sleep(500)
