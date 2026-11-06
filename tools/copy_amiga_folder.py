#!/usr/bin/env python3
"""
copy_amiga_folder.py - copies an Amiga folder (a folder drive, such as
AmiKit's) onto BMAmiga's SD card, with its file names made safe for FAT32
the way Amiberry makes them.

    python3 tools/copy_amiga_folder.py SOURCE DESTINATION [--dry-run]

    python3 tools/copy_amiga_folder.py \\
        /Applications/AmiKit.app/Contents/Resources/AmiKit \\
        /Volumes/BMAMIGA/disks/AmiKit

The Amiga allows characters in file names that FAT32 (and exFAT) don't:
\\ * ? " < > | and control characters, and a name ending in a dot or a
space. Amiberry, on every computer it runs on, stores such a name with
those characters as %xx (their code in hexadecimal), and shows the Amiga
the name as it was; a % itself is %25 (osdep/fsdb_host.cpp,
aname_to_nname()). A folder that was made on a Mac or Linux PC without
going through Amiberry can still have the plain names (macOS and Linux
allow them), and copying it to the card fails for them, or loses the dot.
This copies it with the names Amiberry would have given them:

  - a %xx already there (Amiberry's own, from the Amiga) is kept;
  - the characters above, a % on its own, and a dot or space at the end
    become %xx;
  - each file's .uaem file (Amiberry's protection bits and comment) is
    renamed with its file;
  - accented letters are written as one character each (NFC), as Amiberry
    needs to give them to the Amiga (macOS often keeps them as a letter
    and an accent, which Amiberry can't);
  - the Mac's .DS_Store and ._ files are left out.

Files are copied with their dates (the Amiga sees them). It runs on
macOS, Linux and Windows with Python 3 and nothing else.

This file is part of BMAmiga, a bare metal Amiga emulator for the
Raspberry Pi. See LICENSE for licensing details.
"""

import argparse
import os
import re
import shutil
import sys
import unicodedata

# What Amiberry turns into %xx (fsdb_host.cpp's evilchars, % aside).
EVIL = set('\\*?"/|<>')
HEX = re.compile(r"%[0-9a-fA-F]{2}")
UAEM = ".uaem"


def amiga_name(name):
    """NAME as Amiberry stores it on the card."""
    name = unicodedata.normalize("NFC", name)
    out = []
    i = 0
    while i < len(name):
        c = name[i]
        if c == "%" and HEX.match(name, i):
            out.append(name[i:i + 3])      # already Amiberry's
            i += 3
            continue
        last = i == len(name) - 1
        if c == "%" or c in EVIL or ord(c) < 32 or (last and c in ". "):
            out.append("%%%02x" % ord(c))
        else:
            out.append(c)
        i += 1
    return "".join(out)


def skipped(name):
    return name == ".DS_Store" or name.startswith("._")


def copy_folder(source, dest, dry_run, renamed):
    entries = sorted(os.listdir(source))
    names = set(entries)
    seen = {}
    if not dry_run:
        os.makedirs(dest, exist_ok=True)
    for name in entries:
        if skipped(name):
            continue
        # A file's .uaem goes with the file's new name.
        if name.endswith(UAEM) and name[:-len(UAEM)] in names:
            new = amiga_name(name[:-len(UAEM)]) + UAEM
        else:
            new = amiga_name(name)
            # A name of the Amiga's own ending in .uaem: its last letter as
            # %xx, as Amiberry does, so it isn't taken for another's.
            if new.lower().endswith(UAEM) and not new.endswith("%%%02x" % ord(new[-1])):
                new = new[:-1] + "%%%02x" % ord(new[-1])
        key = new.lower()
        if key in seen:
            print("warning: %s and %s are the same name on the card; %s is left out"
                  % (seen[key], name, os.path.join(source, name)), file=sys.stderr)
            continue
        seen[key] = name
        if new != name:
            renamed.append((os.path.join(source, name), new))
        src = os.path.join(source, name)
        dst = os.path.join(dest, new)
        if os.path.isdir(src) and not os.path.islink(src):
            copy_folder(src, dst, dry_run, renamed)
            if not dry_run:
                shutil.copystat(src, dst, follow_symlinks=False)
        elif os.path.isfile(src):
            if not dry_run:
                shutil.copyfile(src, dst)
                st = os.stat(src)
                os.utime(dst, (st.st_atime, st.st_mtime))


def remove_apple_double(folder):
    removed = 0
    for path, _, files in os.walk(folder):
        for name in files:
            if name.startswith("._"):
                full = os.path.join(path, name)
                try:
                    with open(full, "rb") as f:
                        magic = f.read(4)
                    if magic == b"\x00\x05\x16\x07":
                        os.remove(full)
                        removed += 1
                except OSError:
                    pass
    return removed


def main():
    parser = argparse.ArgumentParser(
        description="Copy an Amiga folder onto BMAmiga's SD card, with names safe for FAT32 "
                    "as Amiberry makes them.")
    parser.add_argument("source", help="the folder to copy (its contents are copied)")
    parser.add_argument("destination", help="the folder on the card to copy into (made if need be)")
    parser.add_argument("--dry-run", action="store_true",
                        help="only list the names that would change")
    args = parser.parse_args()
    if not os.path.isdir(args.source):
        parser.error("%s is not a folder" % args.source)
    renamed = []
    copy_folder(args.source, args.destination, args.dry_run, renamed)
    if not args.dry_run:
        # macOS adds an attribute to the files it writes and, on FAT32,
        # keeps it in a "._" file beside each: the Amiga saw them as files
        # (DEVS:Monitors/._PAL, run by its Startup-Sequence). Only those
        # that are AppleDouble files (their magic number) are removed.
        removed = remove_apple_double(args.destination)
        if removed:
            print("%d of the Mac's ._ file(s) removed from the card" % removed)
    for src, new in renamed:
        print("%s -> %s" % (src, new))
    print("%d name(s) changed%s" % (len(renamed), " (nothing copied: --dry-run)" if args.dry_run else ""))


if __name__ == "__main__":
    main()
