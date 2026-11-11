#!/usr/bin/env python3
"""Builds res/fonts/launchme-icons.ttf: the icons the launcher draws with Segoe Fluent Icons /
Segoe MDL2 Assets on Windows (src/ui/desktop_view.hpp and friends), taken from Microsoft's Fluent
UI System Icons (MIT, https://github.com/microsoft/fluentui-system-icons) and placed at the same
code points. On Linux the DirectWrite port answers "Segoe Fluent Icons" with this font.

Usage: make-icon-font.py <FluentSystemIcons-Regular.ttf> <FluentSystemIcons-Filled.ttf> <out.ttf>
(tools/linux/make-icon-font.sh downloads the fonts and runs this). Needs fontTools.
"""
import sys
from fontTools.ttLib import TTFont, newTable
from fontTools.pens.boundsPen import BoundsPen

# Segoe MDL2 code point -> (Fluent icon name without size, filled?).
ICONS = {
    0xE70D: ("chevron_down", False), 0xE70E: ("chevron_up", False), 0xE70F: ("edit", False), 0xE710: ("add", False),
    0xE711: ("dismiss", False), 0xE712: ("more_horizontal", False), 0xE713: ("settings", False), 0xE716: ("people", False),
    0xE719: ("shopping_bag", False), 0xE71A: ("stop", False), 0xE71C: ("filter", False), 0xE720: ("mic", False),
    0xE721: ("search", False), 0xE72B: ("arrow_left", False), 0xE72C: ("arrow_clockwise", False), 0xE734: ("star", False),
    0xE735: ("star", True), 0xE73E: ("checkmark", False), 0xE74D: ("delete", False), 0xE753: ("cloud", False),
    0xE767: ("speaker_2", False), 0xE768: ("play", False), 0xE769: ("pause", False), 0xE76B: ("chevron_left", False),
    0xE76C: ("chevron_right", False), 0xE770: ("desktop", False), 0xE771: ("paint_brush", False), 0xE7E8: ("power", False),
    0xE7F4: ("tv", False), 0xE7FC: ("games", False), 0xE81C: ("history", False), 0xE838: ("folder_open", False),
    0xE890: ("eye", False), 0xE895: ("arrow_sync", False), 0xE896: ("arrow_download", False), 0xE8A7: ("open", False),
    0xE8B7: ("folder", False), 0xE8B9: ("image", False), 0xE8BB: ("dismiss", False), 0xE8C8: ("copy", False),
    0xE8CB: ("arrow_sort", False), 0xE8EC: ("tag", False), 0xE8F1: ("library", False), 0xE8F2: ("chat_multiple", False),
    0xE921: ("subtract", False), 0xE922: ("maximize", False), 0xE923: ("square_multiple", False), 0xE946: ("info", False),
    0xEA86: ("puzzle_piece", False), 0xED1A: ("eye_off", False),
}
SIZES = (20, 24, 16, 28)


def find(font, name, filled):
    # Glyph names are the icons' names; the cmap is the way that does not depend on them.
    by_name = {}
    for glyph in font.getGlyphOrder():
        by_name[glyph] = glyph
    for glyph in font.getBestCmap().values():
        by_name.setdefault(glyph, glyph)
    for size in SIZES:
        glyph = f"ic_fluent_{name}_{size}_{'filled' if filled else 'regular'}"
        if glyph in by_name:
            return glyph
    sample = [g for g in font.getGlyphOrder() if name in g][:5]
    raise SystemExit(f"no Fluent glyph for {name} ({'filled' if filled else 'regular'}); similar: {sample}")


def main():
    regular, filled, out = TTFont(sys.argv[1]), TTFont(sys.argv[2]), sys.argv[3]
    units = regular["head"].unitsPerEm
    if filled["head"].unitsPerEm != units:
        raise SystemExit("the Fluent fonts disagree on units per em")
    font = TTFont(sys.argv[1])
    for tag in font.keys():  # Every table read now, before the glyph order shrinks.
        if tag != "GlyphOrder":
            font[tag]
    source_glyf, source_hmtx = {False: regular["glyf"], True: filled["glyf"]}, {False: regular["hmtx"], True: filled["hmtx"]}
    first = regular.getGlyphOrder()[0]  # The missing glyph, whatever its name.
    order = [".notdef"]
    glyf, hmtx, cmap = {".notdef": regular["glyf"][first]}, {".notdef": regular["hmtx"][first]}, {}
    for code, (name, is_filled) in sorted(ICONS.items()):
        source = find(filled if is_filled else regular, name, is_filled)
        target = f"uni{code:04X}"
        glyph = source_glyf[is_filled][source]
        if glyph.isComposite():
            raise SystemExit(f"{source} is a composite glyph")
        glyf[target] = glyph
        hmtx[target] = source_hmtx[is_filled][source]
        order.append(target)
        cmap[code] = target

    font.setGlyphOrder(order)
    table = font["glyf"]
    table.glyphs, table.glyphOrder = glyf, order
    font["hmtx"].metrics = hmtx
    for key in ("post",):
        font[key].formatType = 3.0  # No glyph names needed.
    cmap_table = newTable("cmap")
    from fontTools.ttLib.tables._c_m_a_p import cmap_format_4
    sub = cmap_format_4(4)
    sub.platformID, sub.platEncID, sub.language, sub.cmap = 3, 1, 0, cmap
    cmap_table.tableVersion, cmap_table.tables = 0, [sub]
    font["cmap"] = cmap_table
    for drop in ("GSUB", "GPOS", "GDEF", "kern", "LTSH", "VDMX", "hdmx", "DSIG", "meta"):
        if drop in font:
            del font[drop]

    # Centred horizontally in their advance: the Fluent glyphs start at x = 0 (their side padding
    # is in the advance only), so drawn centred in a box they sat left of it, each by its own amount.
    for name in order[1:]:
        glyph, advance = table[name], hmtx[name][0]
        glyph.recalcBounds(table)
        if glyph.numberOfContours <= 0:
            continue
        glyph.coordinates.translate((round((advance - glyph.xMin - glyph.xMax) / 2), 0))
        glyph.recalcBounds(table)
        hmtx[name] = (advance, glyph.xMin)

    # Centred vertically on the icons' common middle, as Segoe's glyphs sit in their line.
    glyph_set = font.getGlyphSet()
    middles = []
    for name in order[1:]:
        pen = BoundsPen(glyph_set)
        glyph_set[name].draw(pen)
        if pen.bounds:
            middles.append((pen.bounds[1] + pen.bounds[3]) / 2)
    middle = sum(middles) / len(middles)
    ascent, descent = round(middle + units / 2), round(units / 2 - middle)
    hhea, os2 = font["hhea"], font["OS/2"]
    hhea.ascent, hhea.descent, hhea.lineGap = ascent, -descent, 0
    os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap = ascent, -descent, 0
    os2.usWinAscent, os2.usWinDescent = max(ascent, 0), max(descent, 0)
    os2.fsSelection |= 1 << 7  # USE_TYPO_METRICS
    os2.usFirstCharIndex, os2.usLastCharIndex = min(cmap), max(cmap)
    hhea.numberOfHMetrics = len(order)
    font["maxp"].numGlyphs = len(order)

    names = font["name"]
    names.names = []
    for name_id, text in ((0, "Icons: Copyright (c) Microsoft Corporation (Fluent UI System Icons, MIT License)."),
                          (1, "Launchme Icons"), (2, "Regular"), (3, "Launchme Icons Regular"), (4, "Launchme Icons"),
                          (5, "Version 1.0"), (6, "LaunchmeIcons-Regular"),
                          (13, "MIT License: https://github.com/microsoft/fluentui-system-icons/blob/main/LICENSE")):
        names.setName(text, name_id, 3, 1, 0x409)
    font.save(out)
    print(f"{out}: {len(cmap)} icons, {units} units per em, ascent {ascent}, descent {descent}")


if __name__ == "__main__":
    main()
