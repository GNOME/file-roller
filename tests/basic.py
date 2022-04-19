#!/usr/bin/env python3

from gi.repository import Gio, GLib
from pathlib import Path
from testutil import test_file_roller

with test_file_roller() as (app, app_tree):
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
