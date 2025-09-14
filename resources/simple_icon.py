#!/usr/bin/env python3
"""
Create a simple ICO file without PIL dependency
"""

def create_simple_ico():
    # Create a minimal but valid 32x32 16-color icon
    # ICO file structure: header + directory entry + bitmap data

    # ICO Header (6 bytes)
    ico_header = bytearray([
        0x00, 0x00,  # Reserved (must be 0)
        0x01, 0x00,  # Type (1 = ICO)
        0x01, 0x00   # Number of images (1)
    ])

    # Directory Entry (16 bytes)
    width = 32
    height = 32
    colors = 0    # 0 means > 8 bits per pixel
    reserved = 0
    planes = 1
    bpp = 32      # 32 bits per pixel

    # Calculate bitmap size: width * height * 4 bytes (RGBA) + some headers
    bitmap_size = 40 + (width * height * 4)  # BITMAPINFOHEADER + pixel data

    directory_entry = bytearray([
        width,                           # Width
        height,                          # Height
        colors,                          # Color count
        reserved,                        # Reserved
        planes & 0xFF, (planes >> 8) & 0xFF,   # Color planes
        bpp & 0xFF, (bpp >> 8) & 0xFF,         # Bits per pixel
        bitmap_size & 0xFF, (bitmap_size >> 8) & 0xFF,
        (bitmap_size >> 16) & 0xFF, (bitmap_size >> 24) & 0xFF,  # Bitmap size
        22, 0x00, 0x00, 0x00             # Offset to bitmap (6 + 16 = 22)
    ])

    # BITMAPINFOHEADER (40 bytes)
    bitmap_header = bytearray([
        40, 0x00, 0x00, 0x00,            # Size of this header (40)
        width, 0x00, 0x00, 0x00,         # Width
        height * 2, 0x00, 0x00, 0x00,    # Height * 2 (includes AND mask)
        0x01, 0x00,                      # Planes
        bpp, 0x00,                       # Bits per pixel
        0x00, 0x00, 0x00, 0x00,          # Compression (0 = none)
        0x00, 0x00, 0x00, 0x00,          # Image size (can be 0 for uncompressed)
        0x00, 0x00, 0x00, 0x00,          # X pixels per meter
        0x00, 0x00, 0x00, 0x00,          # Y pixels per meter
        0x00, 0x00, 0x00, 0x00,          # Colors used
        0x00, 0x00, 0x00, 0x00           # Important colors
    ])

    # Create pixel data (32x32 BGRA format)
    pixels = bytearray()
    center_x, center_y = 16, 16
    radius = 14

    # Create pixels from bottom to top (bitmap format)
    for y in range(height - 1, -1, -1):
        for x in range(width):
            # Calculate distance from center
            dx = x - center_x
            dy = y - center_y
            distance = (dx * dx + dy * dy) ** 0.5

            if distance <= radius:
                # Inside circle - blue with "A"
                if (abs(dx) < 6 and abs(dy) < 8 and
                    ((abs(dy) > 2 and abs(dx) < 3) or  # Vertical lines
                     (abs(dy) < 3 and abs(dy) > 0 and abs(dx) < 5))):  # Horizontal line
                    # White "A" letter
                    pixels.extend([255, 255, 255, 255])  # BGRA: White
                else:
                    # Blue background
                    pixels.extend([255, 85, 45, 255])    # BGRA: Blue
            else:
                # Outside circle - transparent
                pixels.extend([0, 0, 0, 0])              # BGRA: Transparent

    # AND mask (1 bit per pixel, padded to 4-byte boundaries)
    and_mask = bytearray()
    for y in range(height):
        byte_val = 0
        bit_count = 0
        for x in range(width):
            # Transparent pixels have AND mask bit set to 1
            dx = x - center_x
            dy = y - center_y
            distance = (dx * dx + dy * dy) ** 0.5

            if distance > radius:
                byte_val |= (1 << (7 - bit_count))

            bit_count += 1
            if bit_count == 8:
                and_mask.append(byte_val)
                byte_val = 0
                bit_count = 0

        # Pad to complete the byte if needed
        if bit_count > 0:
            and_mask.append(byte_val)

        # Pad row to 4-byte boundary
        while len(and_mask) % 4 != 0:
            and_mask.append(0)

    # Combine all parts
    ico_data = ico_header + directory_entry + bitmap_header + pixels + and_mask

    # Write to file
    with open("app_icon.ico", "wb") as f:
        f.write(ico_data)

    print(f"Created simple icon: app_icon.ico ({len(ico_data)} bytes)")

if __name__ == "__main__":
    create_simple_ico()