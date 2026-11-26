"""Draws docs/demo.gif, the animation at the top of the README.

It is an illustration of what Console to All Nodes does, drawn with Pillow.
It is not a screen recording: the programs only run on Windows.

Needs python3-pil and the Noto Sans and DejaVu Sans Mono fonts (Ubuntu has them).
Usage: python3 tools/make-demo.py docs/demo.gif
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
PREVIEW = sys.argv[2] if len(sys.argv) > 2 else None

W, H, S = 900, 506, 2   # logical size, supersampling factor
NOTO = "/usr/share/fonts/truetype/noto/NotoSans-%s.ttf"
DEJAVU = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono%s.ttf"


def font(path, size):
    return ImageFont.truetype(path, int(size * S))


SANS12 = font(NOTO % "Regular", 12)
SANS13 = font(NOTO % "Regular", 13)
SANS11 = font(NOTO % "Regular", 11)
BOLD13 = font(NOTO % "Bold", 13)
BOLD11 = font(NOTO % "Bold", 11)
BOLD15 = font(NOTO % "Bold", 15)
BOLD16 = font(NOTO % "Bold", 16)
MONO = font(DEJAVU % "", 14)

NODES = ["R%d" % i for i in range(1, 11)] + ["SW1", "SW2"]   # the sorted order

# Colours
DESK = "#1b2130"
CARD = "#ffffff"
CARD_BAR = "#e7eaf0"
INK = "#1f2937"
MUTED = "#6b7280"
LINK = "#b6bfcc"
ROUTER = "#2563eb"
SWITCH = "#0f766e"
BTN, BTN_HOVER, BTN_DOWN = "#2563eb", "#1d4ed8", "#1e3a8a"
TERM_BAR = "#202020"
TERM_BG = "#0c0c0c"
TERM_FG = "#cccccc"
TAB_FG = "#a6a6a6"
CAP_BG = "#0f1420"

BUTTON = (566, 68, 748, 100)
BUTTON_C = (657, 84)
TERM = (40, 20, 860, 434)
TAB_X0, TAB_W, TAB_GAP = 50, 52, 2


def sc(v):
    return [c * S for c in v]


def rrect(d, box, r, fill=None, outline=None, width=1, corners=None):
    d.rounded_rectangle(sc(box), radius=r * S, fill=fill, outline=outline,
                        width=width * S, corners=corners)


def text(d, xy, s, f, fill, anchor="la"):
    d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor)


def line(d, pts, fill, width=1):
    d.line([c * S for p in pts for c in p], fill=fill, width=int(width * S))


def node_pos():
    pos = {}
    for i in range(5):
        pos["R%d" % (i + 1)] = (200 + i * 125, 172)
        pos["R%d" % (i + 6)] = (200 + i * 125, 262)
    pos["SW1"] = (325, 350)
    pos["SW2"] = (575, 350)
    return pos


POS = node_pos()
LINKS = ([("R%d" % i, "R%d" % (i + 1)) for i in (1, 2, 3, 4, 6, 7, 8, 9)] +
         [("R%d" % i, "R%d" % (i + 5)) for i in range(1, 6)] +
         [("SW1", "R6"), ("SW1", "R7"), ("SW1", "R8"),
          ("SW2", "R8"), ("SW2", "R9"), ("SW2", "R10")])


def draw_lab(d, btn):
    rrect(d, (130, 24, 770, 420), 10, fill=CARD)
    rrect(d, (130, 24, 770, 56), 10, fill=CARD_BAR, corners=(True, True, False, False))
    for i, c in enumerate(("#c3c9d4", "#c3c9d4", "#c3c9d4")):
        x = 150 + i * 18
        d.ellipse(sc((x - 5, 35, x + 5, 45)), fill=c)
    rrect(d, (220, 31, 680, 49), 9, fill="#ffffff")
    text(d, (234, 40), "EVE-NG lab", SANS11, MUTED, "lm")

    text(d, (154, 84), "Lab topology", BOLD15, INK, "lm")
    text(d, (262, 85), "12 nodes", SANS12, MUTED, "lm")
    fill = {"idle": BTN, "hover": BTN_HOVER, "down": BTN_DOWN}[btn]
    rrect(d, BUTTON, 6, fill=fill)
    text(d, BUTTON_C, "Console to All Nodes", BOLD13, "#ffffff", "mm")

    for a, b in LINKS:
        line(d, [POS[a], POS[b]], LINK, 1.5)
    for name, (x, y) in POS.items():
        if name.startswith("SW"):
            rrect(d, (x - 17, y - 13, x + 17, y + 13), 5, fill="#ffffff", outline=SWITCH, width=2)
            colour = SWITCH
        else:
            d.ellipse(sc((x - 16, y - 16, x + 16, y + 16)), fill="#ffffff",
                      outline=ROUTER, width=2 * S)
            colour = ROUTER
        text(d, (x, y), name, BOLD13 if len(name) < 3 else BOLD11, colour, "mm")


def tab_box(i):
    x = TAB_X0 + i * (TAB_W + TAB_GAP)
    return (x, 27, x + TAB_W, 56)


def tab_center(i):
    b = tab_box(i)
    return ((b[0] + b[2]) // 2, 42)


def draw_term(img, term):
    # Dim what is behind, then a soft shadow, then the window.
    shade = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(shade)
    sd.rectangle(sc((0, 0, W, 448)), fill=(8, 10, 16, 150))
    for k in range(6, 0, -1):
        sd.rounded_rectangle(sc((TERM[0] - k, TERM[1] - k + 4, TERM[2] + k, TERM[3] + k + 4)),
                             radius=(10 + k) * S, fill=(0, 0, 0, 18 + 6 * (6 - k)))
    img.alpha_composite(shade)
    d = ImageDraw.Draw(img)

    rrect(d, TERM, 9, fill=TERM_BG)
    rrect(d, (TERM[0], TERM[1], TERM[2], 56), 9, fill=TERM_BAR, corners=(True, True, False, False))

    n, active = term["n"], term["active"]
    for i in range(n):
        b = tab_box(i)
        if i == active:
            rrect(d, b, 6, fill=TERM_BG, corners=(True, True, False, False))
            text(d, tab_center(i), NODES[i], BOLD13, "#ffffff", "mm")
        else:
            text(d, tab_center(i), NODES[i], SANS13, TAB_FG, "mm")
            if i + 1 != active and i + 1 < n:
                line(d, [(b[2] + 1, 34), (b[2] + 1, 50)], "#3a3a3a", 1)
    # New-tab plus and the window buttons.
    px = TAB_X0 + n * (TAB_W + TAB_GAP) + 14
    line(d, [(px - 5, 42), (px + 5, 42)], TAB_FG, 1.2)
    line(d, [(px, 37), (px, 47)], TAB_FG, 1.2)
    cx = TERM[2] - 24
    line(d, [(cx - 5, 33), (cx + 5, 43)], TAB_FG, 1.2)
    line(d, [(cx - 5, 43), (cx + 5, 33)], TAB_FG, 1.2)
    cx -= 40
    d.rectangle(sc((cx - 5, 33, cx + 5, 43)), outline=TAB_FG, width=int(1.2 * S))
    cx -= 40
    line(d, [(cx - 5, 38), (cx + 5, 38)], TAB_FG, 1.2)

    x0, y0, lh = 56, 70, 20
    lines = term["lines"]
    for k, s in enumerate(lines):
        text(d, (x0, y0 + k * lh), s, MONO, TERM_FG)
    if term.get("caret", True) and lines:
        cw = MONO.getlength("M") / S
        cxp = x0 + cw * len(lines[-1])
        cy = y0 + (len(lines) - 1) * lh
        d.rectangle(sc((cxp, cy + 1, cxp + cw - 1, cy + 17)), fill=TERM_FG)


def draw_cursor(d, xy):
    x, y = xy
    shape = [(0, 0), (0, 17), (4.2, 13.2), (7.2, 19.6), (9.8, 18.4), (6.9, 12.2), (12.4, 12.2)]
    pts = [((x + px) * S, (y + py) * S) for px, py in shape]
    d.polygon(pts, fill="#ffffff", outline="#111111", width=int(1.2 * S))


def render(st):
    img = Image.new("RGBA", (W * S, H * S), DESK)
    d = ImageDraw.Draw(img)
    draw_lab(d, st["btn"])
    if st["term"]:
        draw_term(img, st["term"])
        d = ImageDraw.Draw(img)
    d.rectangle(sc((0, 448, W, H)), fill=CAP_BG)
    text(d, (W // 2, 476), st["cap"], BOLD16, "#f3f4f6", "mm")
    text(d, (W - 16, 494), "illustration", SANS11, "#7c8597", "rm")
    if st["cursor"]:
        draw_cursor(d, st["cursor"])
    return img.convert("RGB").resize((W, H), Image.Resampling.LANCZOS)


def glide(a, b, steps):
    out = []
    for k in range(1, steps + 1):
        t = k / steps
        t = t * t * (3 - 2 * t)
        out.append((round(a[0] + (b[0] - a[0]) * t), round(a[1] + (b[1] - a[1]) * t)))
    return out


CAP1 = "In EVE-NG, choose Console to All Nodes"
CAP2 = "One Windows Terminal window opens, with a tab for every node"
CAP3 = "Tabs are named after the nodes and sorted: R2 comes before R10"
CAP4 = "Every tab is a live console to its node"

frames = []   # (state, milliseconds)
st = dict(btn="idle", term=None, cap=CAP1, cursor=(430, 300))


def add(ms, **kw):
    st.update(kw)
    frames.append((dict(st, term=dict(st["term"]) if st["term"] else None), ms))


# 1. The click.
add(900)
for p in glide((430, 300), BUTTON_C, 6):
    add(60, cursor=p, btn="hover" if p[1] < 102 and p[0] > 566 else "idle")
add(350, btn="hover")
add(170, btn="down")
add(260, btn="hover")

# 2. The window, with tabs arriving in sorted order. The newest tab has the focus.
for n in range(1, len(NODES) + 1):
    add(90, cap=CAP2, term=dict(n=n, active=n - 1, lines=[""]))
add(350)
add(1300, term=dict(n=12, active=11, lines=["", "SW2>"]))
add(2000, cap=CAP3)

# 3. Switch to R1 and run a command.
for p in glide(BUTTON_C, (tab_center(0)[0] + 4, 46), 7):
    add(60, cursor=p)
add(250)
add(600, cap=CAP4, term=dict(n=12, active=0, lines=["", "R1>"]))
CMD = "show ip interface brief"
for k in range(1, len(CMD) + 1):
    add(60, term=dict(n=12, active=0, lines=["", "R1>" + CMD[:k]]))
add(300)
OUTPUT = [
    "",
    "R1>" + CMD,
    "Interface              IP-Address      OK? Method Status                Protocol",
    "Ethernet0/0            10.0.12.1       YES NVRAM  up                    up",
    "Ethernet0/1            10.0.16.1       YES NVRAM  up                    up",
    "Ethernet0/2            unassigned      YES NVRAM  administratively down down",
    "Loopback0              10.255.0.1      YES NVRAM  up                    up",
    "R1>",
]
add(3200, term=dict(n=12, active=0, lines=OUTPUT))

images = [render(s) for s, _ in frames]
durations = [ms for _, ms in frames]

# One palette for every frame, taken from a sheet of the distinct-looking frames.
keys = [images[0], images[8], images[10], images[23], images[-1]]
sheet = Image.new("RGB", (W, H * len(keys)))
for i, im in enumerate(keys):
    sheet.paste(im, (0, i * H))
palette = sheet.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
pal = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in images]

os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
pal[0].save(OUT, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=False)
print("%d frames, %.1f s, %d bytes" % (len(pal), sum(durations) / 1000, os.path.getsize(OUT)))

if PREVIEW:
    os.makedirs(PREVIEW, exist_ok=True)
    for name, i in (("1-lab", 0), ("2-click", 8), ("3-tabs-arriving", 15), ("4-sorted", 24), ("5-command", len(images) - 1)):
        pal[i].convert("RGB").save(os.path.join(PREVIEW, name + ".png"))
