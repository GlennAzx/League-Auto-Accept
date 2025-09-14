#!/usr/bin/env python3
"""
Script to create a simple application icon for League Auto-Accept
Creates a multi-resolution ICO file with a simple design
"""

from PIL import Image, ImageDraw, ImageFont
import os

def create_icon():
    # Create icon in multiple sizes for Windows
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = []

    for size in sizes:
        # Create new image with transparent background
        img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        # Define colors
        bg_color = (45, 85, 255, 255)      # League blue
        accent_color = (200, 155, 60, 255) # Gold accent
        text_color = (255, 255, 255, 255)  # White

        # Calculate dimensions based on size
        margin = max(2, size // 16)
        inner_size = size - (margin * 2)

        # Draw main background circle
        draw.ellipse([margin, margin, size - margin, size - margin],
                    fill=bg_color, outline=accent_color, width=max(1, size // 32))

        # Draw checkmark or "A" for Auto-Accept
        if size >= 32:
            # Draw "A" for Auto-Accept
            try:
                # Try to use a system font
                font_size = max(8, size // 3)
                font = ImageFont.truetype("arial.ttf", font_size)
            except:
                # Fallback to default font
                font = ImageFont.load_default()

            # Calculate text position to center it
            text = "A"
            bbox = draw.textbbox((0, 0), text, font=font)
            text_width = bbox[2] - bbox[0]
            text_height = bbox[3] - bbox[1]
            text_x = (size - text_width) // 2
            text_y = (size - text_height) // 2 - bbox[1]

            draw.text((text_x, text_y), text, fill=text_color, font=font)
        else:
            # For small sizes, just draw a simple checkmark
            check_size = inner_size // 3
            center_x, center_y = size // 2, size // 2

            # Simple checkmark path
            points = [
                (center_x - check_size//2, center_y),
                (center_x - check_size//4, center_y + check_size//2),
                (center_x + check_size//2, center_y - check_size//2)
            ]

            # Draw checkmark
            for i in range(len(points) - 1):
                draw.line([points[i], points[i + 1]], fill=text_color, width=max(1, size // 16))

        images.append(img)

    # Save as ICO file
    icon_path = "app_icon.ico"
    images[0].save(icon_path, format='ICO', sizes=[(img.width, img.height) for img in images])

    print(f"Created icon: {icon_path}")
    print(f"Icon sizes: {[img.size for img in images]}")

    # Also save a PNG version for preview
    images[-1].save("app_icon.png", format='PNG')
    print("Created preview: app_icon.png")

if __name__ == "__main__":
    try:
        create_icon()
        print("Icon creation successful!")
    except ImportError:
        print("PIL (Pillow) not found. Install with: pip install Pillow")
        print("Creating a simple placeholder icon instead...")

        # Create a very basic icon without PIL
        import struct

        # Create a simple 32x32 icon in ICO format (basic structure)
        ico_data = bytearray([
            # ICO header
            0, 0,  # Reserved
            1, 0,  # Type (1 = ICO)
            1, 0,  # Number of images

            # Image directory entry
            32,    # Width
            32,    # Height
            0,     # Color count (0 = no palette)
            0,     # Reserved
            1, 0,  # Color planes
            32, 0, # Bits per pixel
            0x38, 0x04, 0, 0,  # Image size (1080 bytes)
            22, 0, 0, 0,       # Image offset
        ])

        # Add a simple bitmap data (would need proper implementation for real use)
        # This is just a placeholder - the Python script above creates a proper icon

        with open("app_icon.ico", "wb") as f:
            f.write(ico_data)
        print("Created basic placeholder icon")
    except Exception as e:
        print(f"Error creating icon: {e}")