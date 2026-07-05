#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""
uf2-to-flash.py

Convert a Raspberry Pi Pico UF2 file into a raw flash image.

This tool implements the simple and practical conversion model used for
QEMU-style emulation:

    UF2 target address 0x10000000 + offset  ->  flash image offset

The output file is not a FAT image. It is a raw representation of the
external QSPI flash contents after UF2 programming.

Supported input model:
    - RP2040 / Pico 1 UF2 files, family ID 0xE48BFF56.
    - RP2350 / Pico 2 UF2 files are detected and converted linearly,
      but RP2350 partition remapping is not implemented.

Usage:
    scripts/uf2-to-flash.py firmware.uf2 flash.img
    scripts/uf2-to-flash.py flash.img firmware.uf2

If the two arguments are provided in reverse order, the .uf2 extension is
used to detect the input file.
"""

import os
import sys
import struct


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30

UF2_BLOCK_SIZE = 512
UF2_HEADER_SIZE = 32

FLAG_NOT_MAIN_FLASH = 0x00000001
FLAG_FAMILY_ID_PRESENT = 0x00002000

RP2040_FAMILY_ID = 0xE48BFF56

# Usual RP2350 / Pico 2 family IDs.
# They are detected for diagnostics. This script does not implement
# RP2350 partition table remapping.
RP2350_FAMILY_IDS = {
    0xE48BFF58: "rp2350-arm-secure",
    0xE48BFF59: "rp2350-arm-nonsecure",
    0xE48BFF5A: "rp2350-riscv",
    0xE48BFF5B: "data-or-absolute",
}

XIP_BASE = 0x10000000

# RP2040 UF2 flash window: 0x10000000 .. 0x11000000
PICO1_FLASH_WINDOW_SIZE = 16 * 1024 * 1024

# RP2350 UF2 flash window: 0x10000000 .. 0x12000000
PICO2_FLASH_WINDOW_SIZE = 32 * 1024 * 1024


def print_help() -> None:
    """Print a minimal usage message."""

    program_name = os.path.basename(sys.argv[0])

    print("Usage:")
    print(f"  {program_name} firmware.uf2 flash.img")
    print()
    print("Convert a Raspberry Pi Pico UF2 file into a raw flash image.")
    print("Exactly one argument must have the .uf2 extension, the other")
    print("is used as the raw flash output file.")
    print()
    print("Examples:")
    print(f"  {program_name} blink.uf2 flash.img")
    print(f"  {program_name} flash.raw blink.uf2")


def get_extension(path: str) -> str:
    """Return the lowercase extension of a path."""

    return os.path.splitext(path)[1].lower()


def detect_input_output(arguments: list[str]) -> tuple[str, str]:
    """
    Detect the UF2 input file and the raw image output file.

    The arguments may be passed in either order, provided exactly one of them
    has the .uf2 extension.
    """

    if len(arguments) != 2:
        print_help()
        sys.exit(2)

    first_path, second_path = arguments
    first_ext = get_extension(first_path)
    second_ext = get_extension(second_path)

    if first_ext == ".uf2" and second_ext != ".uf2":
        return first_path, second_path

    if second_ext == ".uf2" and first_ext != ".uf2":
        print("Reversed arguments detected: using the .uf2 file as input.")
        return second_path, first_path

    print("Error: unable to determine which file is the UF2 input.")
    print("Exactly one argument must have the .uf2 extension.")
    print()
    print_help()
    sys.exit(2)


def read_uf2_blocks(path: str):
    """
    Yield all 512-byte blocks from a UF2 file.

    Raises ValueError if the file size is not a multiple of 512 bytes.
    """

    file_size = os.path.getsize(path)

    if file_size == 0:
        raise ValueError("empty file")

    if file_size % UF2_BLOCK_SIZE != 0:
        raise ValueError(
            f"invalid UF2 file size: {file_size} bytes; "
            f"expected a multiple of {UF2_BLOCK_SIZE}"
        )

    with open(path, "rb") as input_file:
        physical_block_index = 0

        while True:
            block = input_file.read(UF2_BLOCK_SIZE)

            if not block:
                break

            physical_block_index += 1

            if len(block) != UF2_BLOCK_SIZE:
                raise ValueError(
                    f"incomplete block at physical index {physical_block_index}"
                )

            yield physical_block_index, block


def analyze_uf2(uf2_path: str) -> dict:
    """
    Analyze a UF2 file and extract main-flash pages.

    Returns a dictionary containing:
        - pages: mapping from flash offset to payload bytes
        - family_ids: detected family IDs
        - warnings: non-fatal problems
        - errors: fatal format problems
        - min_offset / max_end_offset: occupied flash range
    """

    pages: dict[int, bytes] = {}
    warnings: list[str] = []
    errors: list[str] = []

    family_ids: set[int] = set()
    valid_block_count = 0
    non_uf2_block_count = 0
    ignored_not_main_flash_count = 0

    min_offset = None
    max_end_offset = 0

    announced_num_blocks = None
    seen_block_numbers: set[int] = set()

    for physical_index, block in read_uf2_blocks(uf2_path):
        (
            magic_start0,
            magic_start1,
            flags,
            target_address,
            payload_size,
            block_number,
            num_blocks,
            family_or_file_size,
        ) = struct.unpack_from("<IIIIIIII", block, 0)

        (magic_end,) = struct.unpack_from("<I", block, UF2_BLOCK_SIZE - 4)

        if (
            magic_start0 != UF2_MAGIC_START0
            or magic_start1 != UF2_MAGIC_START1
            or magic_end != UF2_MAGIC_END
        ):
            non_uf2_block_count += 1
            errors.append(
                f"block {physical_index}: invalid UF2 magic values"
            )
            continue

        valid_block_count += 1

        if announced_num_blocks is None:
            announced_num_blocks = num_blocks
        elif num_blocks != announced_num_blocks:
            warnings.append(
                f"block {physical_index}: inconsistent numBlocks "
                f"({num_blocks}, expected {announced_num_blocks})"
            )

        if block_number in seen_block_numbers:
            warnings.append(
                f"block {physical_index}: duplicated blockNo {block_number}"
            )

        seen_block_numbers.add(block_number)

        if flags & FLAG_NOT_MAIN_FLASH:
            ignored_not_main_flash_count += 1
            continue

        if not (flags & FLAG_FAMILY_ID_PRESENT):
            errors.append(
                f"block {physical_index}: missing family ID; Pico boot ROM "
                f"expects a family ID"
            )
            continue

        family_id = family_or_file_size
        family_ids.add(family_id)

        if payload_size != 256:
            errors.append(
                f"block {physical_index}: payloadSize={payload_size}; "
                f"expected 256 for Pico UF2 files"
            )
            continue

        if target_address % 256 != 0:
            errors.append(
                f"block {physical_index}: target address is not "
                f"256-byte aligned: 0x{target_address:08x}"
            )
            continue

        pico2_window_end = XIP_BASE + PICO2_FLASH_WINDOW_SIZE
        if not (XIP_BASE <= target_address < pico2_window_end):
            errors.append(
                f"block {physical_index}: target address outside Pico "
                f"flash window: 0x{target_address:08x}"
            )
            continue

        flash_offset = target_address - XIP_BASE

        if flash_offset + payload_size > PICO2_FLASH_WINDOW_SIZE:
            errors.append(
                f"block {physical_index}: page exceeds Pico 2 flash window: "
                f"offset=0x{flash_offset:x}, size={payload_size}"
            )
            continue

        if flash_offset in pages:
            warnings.append(
                f"block {physical_index}: duplicated flash page at offset "
                f"0x{flash_offset:x}; last occurrence wins"
            )

        payload = block[UF2_HEADER_SIZE:UF2_HEADER_SIZE + payload_size]
        pages[flash_offset] = payload

        if min_offset is None or flash_offset < min_offset:
            min_offset = flash_offset

        if flash_offset + payload_size > max_end_offset:
            max_end_offset = flash_offset + payload_size

    if valid_block_count == 0:
        errors.append("no valid UF2 block found")

    if (announced_num_blocks is not None
            and len(seen_block_numbers) != announced_num_blocks):
        warnings.append(
            f"observed {len(seen_block_numbers)} distinct blockNo values, "
            f"but numBlocks announces {announced_num_blocks}"
        )

    return {
        "pages": pages,
        "family_ids": family_ids,
        "warnings": warnings,
        "errors": errors,
        "valid_block_count": valid_block_count,
        "non_uf2_block_count": non_uf2_block_count,
        "ignored_not_main_flash_count": ignored_not_main_flash_count,
        "min_offset": min_offset,
        "max_end_offset": max_end_offset,
    }


def diagnose_family_ids(
    family_ids: set[int],
    warnings: list[str],
    errors: list[str],
) -> None:
    """
    Diagnose UF2 family IDs with respect to Pico 1 and Pico 2.

    RP2040 is accepted as the strict Pico 1 case.
    RP2350 IDs are accepted with a warning because partition remapping is not
    implemented.
    """

    if not family_ids:
        errors.append("no usable family ID found")
        return

    if len(family_ids) > 1:
        family_list = ", ".join(
            f"0x{value:08x}" for value in sorted(family_ids)
        )
        warnings.append(f"multiple family IDs found: {family_list}")

    for family_id in sorted(family_ids):
        if family_id == RP2040_FAMILY_ID:
            continue

        if family_id in RP2350_FAMILY_IDS:
            warnings.append(
                f"family ID 0x{family_id:08x} detected "
                f"({RP2350_FAMILY_IDS[family_id]}): this is probably a "
                f"Pico 2 / RP2350 UF2 file. Linear conversion is performed, "
                f"but RP2350 partition remapping is not implemented."
            )
        else:
            warnings.append(
                f"unknown or non-Pico family ID: 0x{family_id:08x}"
            )

    if RP2040_FAMILY_ID not in family_ids:
        warnings.append(
            "no RP2040 / Pico 1 family ID found (0xe48bff56); "
            "the output may not be a strict Pico 1 flash image."
        )


def build_flash_image(pages: dict[int, bytes], occupied_size: int) -> bytearray:
    """
    Build a raw flash image initialized with erased flash bytes, 0xff.

    Only the occupied part is emitted:
        output size = highest written offset + payload size
    """

    image = bytearray([0xFF] * occupied_size)

    for flash_offset, payload in pages.items():
        image[flash_offset:flash_offset + len(payload)] = payload

    return image


def human_size(byte_count: int) -> str:
    """Return a human-readable size string."""

    if byte_count < 1024:
        return f"{byte_count} B"

    if byte_count < 1024 * 1024:
        return f"{byte_count / 1024:.1f} KiB"

    return f"{byte_count / (1024 * 1024):.2f} MiB"


def main() -> None:
    """Program entry point."""

    uf2_path, image_path = detect_input_output(sys.argv[1:])

    try:
        analysis = analyze_uf2(uf2_path)
    except OSError as error:
        print(f"UF2 file access error: {error}")
        sys.exit(1)
    except ValueError as error:
        print(f"UF2 format error: {error}")
        sys.exit(1)

    pages = analysis["pages"]
    family_ids = analysis["family_ids"]
    warnings = analysis["warnings"]
    errors = analysis["errors"]

    diagnose_family_ids(family_ids, warnings, errors)

    if not pages:
        errors.append("no main-flash page to write")

    if analysis["max_end_offset"] > PICO1_FLASH_WINDOW_SIZE:
        warnings.append(
            "image exceeds the Pico 1 flash window "
            "0x10000000..0x11000000; it is not strict Pico 1 compatible"
        )

    if errors:
        print("Conversion refused: UF2 format problems were found.")
        for error in errors:
            print(f"ERROR: {error}")
        for warning in warnings:
            print(f"WARNING: {warning}")
        sys.exit(1)

    occupied_size = analysis["max_end_offset"]
    image = build_flash_image(pages, occupied_size)

    try:
        with open(image_path, "wb") as output_file:
            output_file.write(image)
    except OSError as error:
        print(f"Output image write error: {error}")
        sys.exit(1)

    programmed_size = sum(len(payload) for payload in pages.values())

    print(f"UF2 input: {uf2_path}")
    print(f"Raw output: {image_path}")
    print(f"Valid UF2 blocks: {analysis['valid_block_count']}")
    print("Ignored NOT_MAIN_FLASH blocks: "
          f"{analysis['ignored_not_main_flash_count']}")
    print(f"Written flash pages: {len(pages)}")
    print(
        f"Actually programmed bytes: {programmed_size} "
        f"({human_size(programmed_size)})"
    )
    print(
        f"Occupied flash size: {occupied_size} "
        f"({human_size(occupied_size)})"
    )

    if family_ids:
        family_id_list = ", ".join(
            f"0x{value:08x}" for value in sorted(family_ids)
        )
        print(f"Family ID(s): {family_id_list}")

    if warnings:
        print()
        print("Warnings:")
        for warning in warnings:
            print(f"- {warning}")


if __name__ == "__main__":
    main()
