#!/usr/bin/env python3
#
# Copyright (c) 2021 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

"""
Process ELF file to generate placeholders for kobject
hash table and lookup functions produced by gperf,
since their sizes depend on how many kobjects have
been declared. The output header files will be used
during linking for intermediate output binaries so
that the addresses of these kobjects would remain
the same during later stages of linking.
"""

import argparse
import os
import sys

import elftools
from elftools.elf.elffile import ELFFile
from packaging import version

if version.parse(elftools.__version__) < version.parse('0.24'):
    sys.exit("pyelftools is out of date, need version 0.24 or later")


def write_define(out_fp, prefix, name, value):
    """Write the #define to output file"""
    define_name = f"KOBJECT_{prefix}_{name}"
    out_fp.write(f"#ifndef {define_name}\n")
    out_fp.write(f"#define {define_name} {value}\n")
    out_fp.write("#endif\n\n")


def output_simple_header(one_sect):
    """Write the header for kobject section"""

    out_fn = os.path.join(args.outdir, f"linker-kobject-prebuilt-{one_sect['name']}.h")
    with open(out_fn, "w") as out_fp:
        if one_sect['exists']:
            align = one_sect['align']
            size = one_sect['size']
            prefix = one_sect['define_prefix']

            write_define(out_fp, prefix, 'ALIGN', align)
            write_define(out_fp, prefix, 'SZ', size)


DATA_REL_SECTIONS = {
    ".data.rel": ".data",
    ".data.rel.local": ".data",
}


def round_up(value, align):
    """Round value up to a multiple of align"""
    if align <= 1:
        return value
    return (value + align - 1) // align * align


def generate_linker_headers(obj):
    """Generate linker header files to be included by the linker script"""

    # Sections we are interested in
    sections = {
        ".data": {
            "name": "data",
            "define_prefix": "DATA",
            "exists": False,
            "multiplier": int(args.datapct) + 100,
        },
        ".rodata": {
            "name": "rodata",
            "define_prefix": "RODATA",
            "exists": False,
            "extra_bytes": args.rodata,
        },
        ".priv_stacks.noinit": {
            "name": "priv-stacks",
            "define_prefix": "PRIV_STACKS",
            "exists": False,
        },
    }

    for one_sect in obj.iter_sections():
        # REALLY NEED to match exact type as all other sections
        # (symbol, debug, etc.) are descendants where
        # isinstance() would match.
        if type(one_sect) is not elftools.elf.sections.Section:  # pylint: disable=unidiomatic-typecheck
            continue

        # Position independent code puts data with relocations, like the
        # gperf table on 64-bit hosts, in .data.rel sections, which end up
        # in the kobject data area as well.
        name = DATA_REL_SECTIONS.get(one_sect.name, one_sect.name)
        if name in sections:
            # Need section alignment and size
            sect = sections[name]
            sect['align'] = max(sect.get('align', 0), one_sect['sh_addralign'])
            sect['size'] = round_up(sect.get('size', 0), one_sect['sh_addralign'])
            sect['size'] += one_sect['sh_size']
            sect['exists'] = True

    for sect in sections.values():
        if not sect['exists']:
            continue

        if "multiplier" in sect:
            sect['size'] *= sect['multiplier'] / 100
            sect['size'] = int(sect['size'])

        if "extra_bytes" in sect:
            sect['size'] += int(sect['extra_bytes'])

    for one_sect in sections:
        output_simple_header(sections[one_sect])


def parse_args():
    """Parse command line arguments"""
    global args

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )

    parser.add_argument("--object", required=True, help="Points to kobject_prebuilt_hash.c.obj")
    parser.add_argument(
        "--outdir", required=True, help="Output directory (<build_dir>/include/generated)"
    )
    parser.add_argument(
        "--datapct", required=True, help="Multiplier to the size of reserved space for DATA region"
    )
    parser.add_argument("--rodata", required=True, help="Extra bytes to reserve for RODATA region")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose messages")
    args = parser.parse_args()
    if "VERBOSE" in os.environ:
        args.verbose = 1


def main():
    """Main program"""
    parse_args()

    with open(args.object, "rb") as obj_fp:
        obj = ELFFile(obj_fp)

        generate_linker_headers(obj)


if __name__ == "__main__":
    main()
