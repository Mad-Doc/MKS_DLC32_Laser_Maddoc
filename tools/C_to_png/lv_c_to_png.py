"""
Convertit les fichiers .c LVGL en PNG.
Prend en charge LV_COLOR_DEPTH 16 (RGB565) et 32.
Usage : python lv_c_to_png.py SP_H_UP.c SP_L_UP.c
"""
import os
import re
import struct
import sys
import zlib

def _extract_depth_block(src, depth):
    # Capture the LV_COLOR_DEPTH-specific data section.
    match = re.search(
        rf"#(?:if|elif)\s+LV_COLOR_DEPTH\s*==\s*{depth}([\s\S]*?)(?=#elif\s+LV_COLOR_DEPTH|#endif)",
        src,
    )
    if not match:
        return None
    return match.group(1)


def _extract_hex_data_for_depth(src, depth, color16_swap):
    block = _extract_depth_block(src, depth)
    if block is None:
        raise ValueError(f"Bloc LV_COLOR_DEPTH == {depth} introuvable")

    # For 16-bit exports, converter often embeds two variants depending on swap.
    if depth == 16:
        swap_match = re.search(
            r"#if\s+LV_COLOR_16_SWAP\s*==\s*0([\s\S]*?)#else([\s\S]*?)#endif",
            block,
        )
        if swap_match:
            block = swap_match.group(1) if color16_swap == 0 else swap_match.group(2)

    hex_bytes = re.findall(r"0x([0-9a-fA-F]{2})", block)
    if not hex_bytes:
        raise ValueError("Aucune donnee hex detectee pour la profondeur couleur")

    return bytes(int(b, 16) for b in hex_bytes)


def c_to_png(c_file, color_depth=16, color16_swap=0):
    with open(c_file, 'r', encoding='utf-8', errors='ignore') as f:
        src = f.read()

    # Extraire largeur et hauteur
    w = int(re.search(r'\.header\.w\s*=\s*(\d+)', src).group(1))
    h = int(re.search(r'\.header\.h\s*=\s*(\d+)', src).group(1))

    data = _extract_hex_data_for_depth(src, color_depth, color16_swap)

    pixels = []
    expected_pixels = w * h

    if color_depth == 32:
        data = data[:len(data) - (len(data) % 4)]
        for i in range(0, len(data), 4):
            r, g, b, a = data[i], data[i + 1], data[i + 2], data[i + 3]
            pixels.append((r, g, b, a))
    elif color_depth == 16:
        data = data[:len(data) - (len(data) % 2)]
        for i in range(0, len(data), 2):
            if color16_swap == 0:
                v = data[i] | (data[i + 1] << 8)
            else:
                v = data[i + 1] | (data[i] << 8)

            r5 = (v >> 11) & 0x1F
            g6 = (v >> 5) & 0x3F
            b5 = v & 0x1F

            r = (r5 * 255) // 31
            g = (g6 * 255) // 63
            b = (b5 * 255) // 31
            pixels.append((r, g, b, 255))
    else:
        raise ValueError("Profondeur non supportee (utiliser 16 ou 32)")

    if len(pixels) < expected_pixels:
        raise ValueError(f"Donnees image insuffisantes: {len(pixels)} < {expected_pixels}")

    pixels = pixels[:expected_pixels]

    # Écrire PNG brut
    def png_chunk(name, data):
        c = name + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

    rows = bytearray()
    for y in range(h):
        rows.append(0)  # filter type None
        for x in range(w):
            r, g, b, a = pixels[y * w + x]
            rows += bytes([r, g, b, a])

    # Use RGBA (color type 6)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)

    compressed = zlib.compress(bytes(rows))

    out = b'\x89PNG\r\n\x1a\n'
    out += png_chunk(b'IHDR', ihdr)
    out += png_chunk(b'IDAT', compressed)
    out += png_chunk(b'IEND', b'')

    out_file = os.path.splitext(c_file)[0] + '.png'
    with open(out_file, 'wb') as f:
        f.write(out)
    print(f"Converti : {out_file}  ({w}x{h}) depth={color_depth} swap16={color16_swap}")

if __name__ == '__main__':
    # Defaults for this firmware: LV_COLOR_DEPTH=16, LV_COLOR_16_SWAP=0
    files = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
    depth = 16
    swap = 0

    for arg in sys.argv[1:]:
        if arg.startswith("--depth="):
            depth = int(arg.split("=", 1)[1])
        elif arg.startswith("--swap16="):
            swap = int(arg.split("=", 1)[1])

    for f in files:
        c_to_png(f, color_depth=depth, color16_swap=swap)
