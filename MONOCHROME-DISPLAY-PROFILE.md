# Monochrome display profile

LoRagotchi-DX records the physical panel fitted to a monochrome host. It does not attempt to recolour one-bit OLED hardware.

## Display menu

On monochrome hosts, **Display → Screen colour** offers:

- **Blue OLED** — default; SSD1306/SH1106 128×64 panels, including the common Heltec V3-style blue OLED.
- **White OLED** — SSD1306/SH1106 128×64 white panels.
- **Yellow/Blue OLED** — SSD1306-compatible two-tone panels: yellow top quarter and blue lower three quarters.

A dismissible first-run screen explains the choice. Dismissing it accepts the Blue OLED default; selecting any profile persists it.

## Visiting colour hosts

The selected profile travels as a backwards-compatible `P1` extension on the existing DX commit message. Colour-capable hosts retain the profile alongside the monochrome visitor metadata, so they can render or label a visitor consistently. Older hosts ignore the trailing extension and continue using the existing tint field.
