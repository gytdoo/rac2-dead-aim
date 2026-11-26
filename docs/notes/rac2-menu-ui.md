# rac2 (Going Commando, ntsc2 v2.00) mod menu UI: glyphs, renderer, limits

Sources: disc ELF `SCUS_972.68` (VER 2.00, read from the USA disc), the port binary
`bin/openrac-rac2-ntsc2` (OpenRAC 1.1.0, not stripped; disassembled, not run),
rac2-decomp `src/game/0027F128-font-g8.c` (text_draw_line), `symbol_addrs/usa-v2`,
openrac-mods `xbox-icons/src/rac2/rac2.c`, SDK 1.20.0 / 1.36.0 headers and MODS.md.

## Font glyph tables (ntsc2)

Three glyph tables of 256 x 4 bytes (u, v, top, advance): 0x00263B10, 0x00263EB0, 0x00264250
(v1.01: 0x00263B50 / EF0 / 290). The three fonts' icon entries are identical. Icons are
cells of row v=96 in the 256x128 8-bit font textures.

| byte | icon | cell u | advance |
|---|---|---|---|
| 0x10 | Cross | 0 | 18 |
| 0x11 | Circle | 72 | 18 |
| 0x12 | Triangle | 48 | 18 |
| 0x13 | Square | 24 | 18 |
| 0x14 | L1 | 96 | 25 |
| 0x15 | R1 | 144 | 25 |
| 0x16 | L2 | 120 | 25 |
| 0x17 | R2 | 168 | 25 |
| 0x18 | left stick (L3) | 208 | 16 |
| 0x19 | right stick (R3) | 232 | 16 |

Same as SDK `OPENRAC_UI_CROSS` "\x10" ... `OPENRAC_UI_RIGHT_STICK` "\x19" (the L1..RS macros
are only in the 1.31+ header, but the bytes work on any version: the port passes them through).

Printable aliases (same cells): `&` 0x26 = Cross, `#` 0x23 = Square, `@` 0x40 = Circle,
`^` 0x5E = Triangle; and 0xF8..0xFF = Cross, Circle, Triangle, Square, L1, R1, L2, R2.
0xB0 points at row 96 u=192 (16 px, adv 14): a cell between R2 and the sticks, not identified.

No glyph for D-pad, Start, Select (the game spells START/SELECT).

How text_draw_line (0x0027F2A8 v1.01) draws:
- bytes < 0x20 with a glyph (0x10..0x19): a 24x16 quad, coloured with the grey average of the
  line colour (alpha kept): the icon keeps its own colours. This is the game's own path.
- printable aliases (& # @ ^, 0xF8..): drawn as letters, a 16x16 quad tinted by the line's
  colour: the 24-px cell is cut to its left 16 px and tinted. Prefer 0x10..0x19.
- 0x08..0x0F: inline colour codes (they are not glyphs), applied only while
  g_textColorCodeEnabled (ntsc2 0x001B15E8) != 0. Slot 0 is the line's own colour (reset).
  The table at 0x001A89B0 (disc values, GS ABGR, 0x80 = 1.0):
  \x08 reset, \x09 blue (70,70,E0), \x0A green (40,A0,40), \x0B purple (A0,60,A0),
  \x0C orange (C0,80,40), \x0D..\x0F black. So '\n' (0x0A) and '\t' (0x09) are colour
  switches, not breaks.
- bytes with advance 0 are skipped silently: % * = [ \ ] _ ` { | } ~ and 0x7F, and
  0x01..0x07, 0x1A..0x1F.
- 0x80..0xA7 are accented letters (base + accent overlay from entry +0x40).

## Port 1.1.0 text conversion (options_menu::game_text, 0xE3F8C0)

ASCII bytes < 0x80 go through unchanged (icons, colour codes, & # @ ^). 2-byte UTF-8 is mapped
through kCodes (0x7B4D30) to the game's 0x80..0xAF: á à â ä é è ê ë í ì î ï ó ò ô ö ú ù û ü,
the same in capitals, Ñ ñ Ç ç ß ¡ ¿. Other 2-byte characters are dropped. 3-byte UTF-8
(… – — → • ©...) is NOT handled: its raw bytes come out as accent marks and wrong letters.

## Port 1.1.0 rac2 page renderer (rac2_mod_pages_draw, 0x1B0EB40)

Used for the title page (ui_main_item) and the pause page (ui_pause_item); both are a copy of
the Camera options page with the port drawing the panel. It calls the game's own
text_draw_centered (us2 0x002801B8 = font 1, 0x00280250 = font 2), so everything is the
game's font and goes through text_draw_line above. Everything is centred at x = 256.

- Title: written into the page's title box (game_text'd, up to 63 bytes kept).
- Rows: view holds up to 12 items. 1..8 rows: 20 px apart from y = 112, font 1.
  9..12 rows: squeezed into 160 px (13..17 px apart), font 2. All rows drawn, no scrolling window.
- A choice is ONE centred line "Label: Value" (snprintf "%s: %s"); a button or text line is just
  its label. No label-left / value-right columns on rac2.
- Row colours (GS ABGR): cursor row 0x80F0F0F0 (bright white), other rows 0x70808080,
  OPENRAC_UI_TEXT rows 0x70B0B0A0 (same as the footer), DISABLED rows 0x40505050 (dim, half
  transparent). Inline colour codes replace the RGB from that point on, so they also hide the
  cursor's highlight for that part of the line.
- Footer: up to 3 lines at y = 300, 316, 332, font 2, colour 0x70B0B0A0. With no footer the
  port shows "\x10 Select   \x12 Back".
- Question (ui_confirm): the question is one line at y = 152 (newlines turned into spaces),
  footer "\x10 Yes   \x12 No".
- Strings are cut at 47 bytes by ui_set (OPENRAC_UI_TEXT_MAX); an icon is one byte.

Font advances (font 1): lower case about 9.5 px, capitals 13, digits 13, space 5. "Camera Speed:
Normal" is about 199 px. Keep a row under about 24 characters to stay in the panel.

## Other drawing at 1.20 on rac2

- hud_text_set: accepted, but nothing in the 1.1.0 rac2 build reads the list (only the rac1
  host draws it). No effect on rac2.
- on_overlay: works on rac2 (frontend present_frame), but only while a level is in play: not over
  menus at 1.20. rect/text/text_width in 512x448; text is the port's font, not the game's.
- menu_entry / menu_rect / menu_text: documented for rac1's title Options; no rac2 front-end entry
  calls the mod menu in 1.1.0 (menu_entry_count has no caller). 5x7 CPU font anyway.
- developer_menu: OpenRAC's own overlay tab (ImGui), not game style.
- Writing guest memory (as xbox-icons does) can repaint font cells and glyph metrics, but that
  changes the game's text everywhere and conflicts with xbox-icons.

## What newer interfaces add (SDK 1.36 in hand; 1.37..1.39 not available locally)

- 1.31: OPENRAC_UI_L1..RIGHT_STICK macros; button tokens in overlay text and HUD text drawn
  per device (icon / pad letter / bound key); button_label() gives the label as text;
  on_overlay_phases with OPENRAC_OVERLAY_MENU draws over the pause menu (needed for a controller
  diagram drawn over or beside a mod page).
- 1.28: developer_menu_look (rows with values, progress bars, icons) in OpenRAC's overlay menu.
- 1.33: overlay set_blend (alpha/add/subtract).
- 1.36: sharp_cells: hi-res pictures for cells a mod paints into the game's font texture (crisp
  custom glyphs, for example a D-pad or Start icon painted into a free cell).
- The ui_page / ui_set / ui_pause_item API and limits (16 pages, 12 items, 3 footer lines, 47 bytes,
  2 pause items) are unchanged through 1.36.
