"""Decode the Xbox framebuffer out of an emulated-RAM dump and check the cube.

Input is a 64 MiB dump of the emulated console's physical memory, produced by
scripts/capture_emulator_frame.ps1 (QEMU monitor `pmemsave`). The probe renders
A8R8G8B8 at 640x480 into pbkit's contiguous framebuffers, so the frame can be
located by its clear colour and written out as a PNG using only the standard
library.

Usage:
    python tools/decode_framebuffer.py <dump> <out_prefix> [--expect-cube]

`--expect-cube` additionally asserts the baseline-pose face visibility. With
model = RotationY(30 deg) * RotationX(-20 deg) and the camera looking down -Z,
exactly three faces turn toward the camera:

    +Z red, -X yellow, -Y magenta

and their opposites must be absent. If back-face culling or the winding
convention were inverted, the opposite set would appear instead; if depth were
wrong, both sets would be mixed.
"""
import struct
import sys
import zlib
from collections import Counter

WIDTH = 640
HEIGHT = 480
PITCH = 2560          # 640 * 4, already 64-byte aligned, matches pbkit's tile pitch
CLEAR_ARGB = 0xFF203040

FRONT_FACES = (("red +Z", 0xFFFF0000), ("yellow -X", 0xFFFFFF00), ("magenta -Y", 0xFFFF00FF))
BACK_FACES = (("green -Z", 0xFF00FF00), ("blue +X", 0xFF0000FF), ("cyan +Y", 0xFF00FFFF))


def write_png(path, rows):
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(tag, data):
        body = struct.pack(">I", len(data)) + tag + data
        return body + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    blob = b"\x89PNG\r\n\x1a\n"
    blob += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
    blob += chunk(b"IDAT", zlib.compress(raw, 6))
    blob += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(blob)


OVERLAY_WHITE = 0x00FFFFFF   # pbkit's text screen colour as stored in the surface


def _white_rows_score(data, start):
    """Score an alignment by how well pbkit's text overlay sits at the top.

    The probe prints three overlay lines starting at the top of the frame, so
    the correct buffer start is the one that puts white text in the first rows
    and none in the last rows. Because the three framebuffers hold the same
    static frame, a wrong alignment only rotates the image, which is invisible
    to a colour histogram but obvious here.
    """
    needle = struct.pack("<I", OVERLAY_WHITE)
    top = bottom = 0
    for y in range(0, 56):
        base = start + y * PITCH
        top += data.count(needle, base, base + WIDTH * 4)
    for y in range(HEIGHT - 56, HEIGHT):
        base = start + y * PITCH
        bottom += data.count(needle, base, base + WIDTH * 4)
    return top - bottom


def _anchors_for(data, argb):
    needle = struct.pack("<I", argb)
    found = set()
    pos = 0
    while True:
        idx = data.find(needle, pos)
        if idx < 0:
            break
        if idx % 4:
            pos = idx + 1
            continue
        end = idx
        while end + 4 <= len(data) and data[end:end + 4] == needle:
            end += 4
        if (end - idx) // 4 >= WIDTH:
            found.add(idx & ~0x3FFF)
        pos = max(end, idx + 4)
    return found


def find_framebuffers(data):
    """Return the best-aligned start of pbkit's framebuffer.

    pbkit allocates front + two back buffers contiguously, 16 KiB aligned, each
    PITCH * HEIGHT bytes, and the probe's frame is static, so every 16 KiB
    aligned window inside the region yields an identical colour histogram and
    only the text overlay reveals the true origin.

    The clear colour normally anchors the search. In the near-plane pose the
    cube engulfs the camera and no background survives, so fall back to the
    face colours, which then form the long runs instead.
    """
    stride = PITCH * HEIGHT
    anchor_argb = CLEAR_ARGB
    anchors = _anchors_for(data, CLEAR_ARGB)
    if not anchors:
        for _name, argb in FRONT_FACES + BACK_FACES:
            anchors = _anchors_for(data, argb)
            if anchors:
                anchor_argb = argb
                print("clear colour absent; anchoring on 0x%08X" % argb)
                break
    if not anchors:
        return []

    def clear_count(start):
        needle_anchor = struct.pack("<I", anchor_argb)
        total = 0
        for y in range(0, HEIGHT, 4):
            base = start + y * PITCH
            total += data.count(needle_anchor, base, base + WIDTH * 4)
        return total

    low = min(anchors)
    high = max(anchors)
    candidates = []
    candidate = max(0, low - stride)
    candidate -= candidate % 0x4000
    limit = min(len(data) - stride, high + stride)
    while candidate <= limit:
        candidates.append((clear_count(candidate), candidate))
        candidate += 0x4000
    if not candidates:
        return []

    # Windows wholly inside the framebuffer region are cyclic rotations of the
    # same static frame, so they share a histogram; windows that straddle the
    # region's edge mix in foreign memory and score lower on the clear colour.
    best_clear = max(c for c, _ in candidates)
    plateau = [s for c, s in candidates if c >= best_clear - 2]
    if not plateau:
        return sorted(anchors)[:1]
    # Among equivalent rotations, the true origin puts the text overlay on top.
    plateau.sort(key=lambda s: -_white_rows_score(data, s))
    return [plateau[0]]


def decode(dump_path, out_prefix, expect_cube=False):
    with open(dump_path, "rb") as fh:
        data = fh.read()
    if len(data) != 0x4000000:
        print("warning: dump is %d bytes, expected 67108864" % len(data))

    starts = find_framebuffers(data)
    if not starts:
        print("FAIL: no framebuffer found (clear colour 0x%08X absent)" % CLEAR_ARGB)
        return 1
    print("framebuffers: %s" % ", ".join("0x%08X" % s for s in starts))

    failures = []
    for index, start in enumerate(starts):
        if start + PITCH * HEIGHT > len(data):
            continue
        region = data[start:start + PITCH * HEIGHT]
        colours = Counter()
        rows = []
        for y in range(HEIGHT):
            base = y * PITCH
            row = bytearray()
            for x in range(WIDTH):
                px = region[base + x * 4: base + x * 4 + 4]
                colours[px] += 1
                blue, green, red, _alpha = px
                row += bytes((red, green, blue))
            rows.append(bytes(row))
        total = float(WIDTH * HEIGHT)

        def share(argb):
            return 100.0 * colours.get(struct.pack("<I", argb), 0) / total

        png = "%s-buf%d.png" % (out_prefix, index)
        write_png(png, rows)
        print("")
        print("buffer %d @ 0x%08X -> %s" % (index, start, png))
        print("   clear %5.2f%%   distinct colours %d" % (share(CLEAR_ARGB), len(colours)))
        for name, argb in FRONT_FACES:
            print("   front %-11s %6.3f%%" % (name, share(argb)))
        for name, argb in BACK_FACES:
            print("   back  %-11s %6.3f%%" % (name, share(argb)))

        if expect_cube:
            for name, argb in FRONT_FACES:
                if share(argb) <= 0.0:
                    failures.append("buffer %d: front face %s missing" % (index, name))
            for name, argb in BACK_FACES:
                if share(argb) > 0.0:
                    failures.append("buffer %d: back face %s visible (%.3f%%)" % (index, name, share(argb)))

    if expect_cube:
        if failures:
            print("\nFAIL:")
            for line in failures:
                print("   " + line)
            return 1
        print("\nPASS: three camera-facing colours present, all opposite faces absent")
    return 0


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sys.exit(decode(args[0], args[1], "--expect-cube" in sys.argv))
