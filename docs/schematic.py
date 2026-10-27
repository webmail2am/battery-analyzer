"""Generates docs/schematic.pdf (2 sheets) for the Li-ion battery analyzer.

    pip install schemdraw matplotlib pypdf
    python docs/schematic.py
"""
# Copyright (c) 2026 Ashoka Munasinghe. MIT License, see LICENSE.
# SPDX-License-Identifier: MIT
import os
import schemdraw
import schemdraw.elements as elm
from pypdf import PdfWriter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "schematic.pdf")
DATE = "2026-10-26"
NET = "#1f4e9a"
GREY = "#555555"
schemdraw.config(fontsize=11, lw=1.3)


def net(d, at, name, direction="right", lead=0.8):
    """Short wire from `at` ending in a net-label tag."""
    ln = d.add(getattr(elm.Line().at(at).length(lead), direction)())
    d.add(getattr(elm.Tag(width=2.6, height=0.55).at(ln.end), direction)()
          .label(name, fontsize=9, color=NET))
    return ln


def text(d, xy, s, size=10, halign="left", color="black"):
    d.add(elm.Label().at(xy).label(s, fontsize=size, halign=halign, valign="top", color=color))


def title_block(d, xy, sheet, title):
    x, y = xy
    d.add(elm.Line().at((x, y)).to((x + 12, y)).color(GREY))
    text(d, (x, y - 0.2), "Li-ion Battery Analyzer", 13)
    text(d, (x, y - 0.9), f"Sheet {sheet}/2 - {title}", 11)
    text(d, (x, y - 1.5), f"Pro Mini 328 + AD7705 + 0.96in I2C OLED   Rev F   {DATE}", 9, color=GREY)


def ic(d, at, w, left, right, label):
    """IC/module block. `left`/`right` are top-to-bottom lists of (pin, net); returns the element."""
    pins = [elm.IcPin(name=p, side="left", anchorname="L%d" % i) for i, (p, _) in enumerate(left)][::-1]
    pins += [elm.IcPin(name=p, side="right", anchorname="R%d" % i) for i, (p, _) in enumerate(right)][::-1]
    u = d.add(elm.Ic(pins=pins, w=w, pinspacing=1.0, edgepadH=0.7).right().at(at)
              .label(label, "top", fontsize=10))
    for i, (_, n) in enumerate(left):
        if n:
            net(d, getattr(u, "L%d" % i), n, "left")
    for i, (_, n) in enumerate(right):
        if n:
            net(d, getattr(u, "R%d" % i), n, "right")
    return u


# ============================================================ Sheet 1 =====
def sheet1(path):
    with schemdraw.Drawing(file=path, show=False) as d:
        ic(d, (0, 0), 5,
           [("RAW", ""), ("VCC", "+3V3"), ("GND", "GND"), ("D2", "BTN_START"), ("D3", "BTN_MODE"),
            ("D4", "BTN_RINT"), ("D5", "ADC_DRDY"), ("D6", "ADC_CS"), ("D7", "CHG_CHRG"), ("D8", "")],
           [("D9", ""), ("D10", ""), ("D11 MOSI", "MOSI"), ("D12 MISO", "MISO"),
            ("D13 SCK", "SCK"), ("A0", ""), ("A1", "LOAD_EN"), ("A2", "CHG_EN"),
            ("A3", "CHG_STDBY"), ("A4 SDA", "SDA"), ("A5 SCL", "SCL")],
           "U1  Arduino Pro Mini\nATmega328P  3.3 V / 8 MHz")

        ic(d, (17, 2), 5,
           [("VCC", "+3V3"), ("GND", "GND"), ("SCLK", "SCK"), ("DIN", "MOSI"),
            ("DOUT", "MISO"), ("CS", "ADC_CS"), ("DRDY", "ADC_DRDY"), ("RST", "+3V3")],
           [("AIN1+", "SHUNT-"), ("AIN1-", "GND"), ("AIN2+", "VSENSE"), ("AIN2-", "GND")],
           "U2  AD7705 module, unmodified\n(LM285-2.5 ref, 10k/10k input\ndividers, AIN- tied to GND)")

        ic(d, (32, 2), 5,
           [("GND", "GND"), ("VCC", "+3V3"), ("SCL", "SCL"), ("SDA", "SDA")],
           [],
           "U3  0.96in OLED 128x64\nSSD1306, 4-pin I2C 0x3C\n(pull-ups + reset on module)")

        # decoupling at the ADC module
        c = d.add(elm.Capacitor().at((30, -2)).down().label("C3\n100nF\n(at U2)", loc="bottom", fontsize=9))
        d.add(elm.Vdd().at(c.start).label("+3V3", fontsize=9))
        d.add(elm.Ground().at(c.end))

        # three push buttons to GND
        buttons = (("S1  START", "BTN_START", "start test / back, hold = abort"),
                   ("S2  MODE", "BTN_MODE", "QUICK -> FULL -> CHG+DIS"),
                   ("S3  RINT", "BTN_RINT", "Rint test / back, hold = abort"))
        for k, (name, sig, desc) in enumerate(buttons):
            bx, by = -4 + 7 * k, -5
            net(d, (bx, by), sig, "left", 0.4)
            b = d.add(elm.Button().at((bx, by)).right()
                      .label(name, loc="top", fontsize=9))
            d.add(elm.Line().at(b.end).right(0.4))
            d.add(elm.Line().down(0.5))
            d.add(elm.Ground())
            text(d, (bx - 3, by - 1.6), desc, 8, color=GREY)
        text(d, (-7, -7.4), "internal pull-ups on D2, D3, D4", 9)

        text(d, (17, -4.5),
             "Notes\n"
             "- +3V3 comes from U6 (sheet 2) into the Pro Mini VCC pin; RAW is unused.\n"
             "- SPI (SCK/MOSI/MISO) is used by the AD7705 only. D8, D9, A0 unused; D10 must stay unconnected.\n"
             "- AD7705 RST tied to +3V3; the firmware resets it over SPI.\n"
             "- OLED on I2C (A4 SDA, A5 SCL); the 4-pin module has its own pull-ups and reset.\n"
             "- GND = star point at the GND end of the shunt Rs (sheet 2).\n"
             "- Program via the Pro Mini FTDI header (DTR, TX, RX, VCC, GND).\n"
             "- Net labels with the same name are connected.", 9)
        title_block(d, (32, -9), 1, "Controller, ADC, display, controls")
    return path


# ============================================================ Sheet 2 =====
def sheet2(path):
    with schemdraw.Drawing(file=path, show=False) as d:
        RAIL = 12
        LX = 16   # load column
        PX = 27   # power column
        CX = 45   # charger column

        # ---------------- cell + shunt in the minus lead
        bat = d.add(elm.BatteryCell().at((0, 6)).up().reverse()
                    .label("B1  Li-ion cell\n(18650 etc.)", loc="bottom", fontsize=10))
        text(d, (-0.7, (bat.start[1] + bat.end[1]) / 2 + 0.7), "+", 14)
        d.add(elm.Line().at(bat.end).toy(RAIL))
        d.add(elm.Line().at(bat.start).toy(4.5))
        d.add(elm.Dot())
        sn = d.here
        d.add(elm.Resistor().down().label("Rs  0.02 ohm\n1 %, 1 W\n(4-terminal if\npossible)", loc="bottom", fontsize=9))
        d.add(elm.Dot())
        sp = d.here
        d.add(elm.Line().toy(0).linewidth(3))
        d.add(elm.Dot().at((0, 0)))
        d.add(elm.Ground().at((0, 0)))
        text(d, (-0.8, -0.3), "STAR GND", 9, halign="right")
        net(d, sn, "SHUNT-", "left", 0.8)
        text(d, (-5.5, sn[1] + 1.4), "cell minus\n(below GND by I x Rs)\nto AD7705 AIN1+", 8, color=GREY)
        text(d, (0.5, sp[1] - 0.3), "GND end = star point\n(AIN1- is GND on the module)", 8, color=GREY)
        d.add(elm.Dot().at((0, RAIL)))
        net(d, (0, RAIL), "BAT+", "left", 0.6)

        # ---------------- Kelvin voltage sense (the AD7705 module divides it by 2)
        d.add(elm.Line().at((0, RAIL)).to((3, RAIL)).color("#b05000"))
        d.add(elm.Line().toy(RAIL - 3).color("#b05000"))
        text(d, (0.4, RAIL - 0.2), "sense wire\n(Kelvin)", 8, color="#b05000")
        net(d, (3, RAIL - 3), "VSENSE", "right", 0.6)
        text(d, (3.6, RAIL - 3.6), "to AD7705 AIN2+\n(module 10k/10k: Vbat / 2)", 8, color=GREY)

        # ---------------- load path
        LR = RAIL + 1.5
        d.add(elm.Line().at((0, RAIL)).toy(LR).linewidth(3))
        d.add(elm.Line().tox(LX).linewidth(3))
        d.add(elm.Line().toy(RAIL).linewidth(3))
        text(d, (6, LR + 0.6), "load current (heavy wire)", 8, color=GREY)
        rl = d.add(elm.ResistorVar().at((LX, RAIL)).down()
                   .label("RL  external load (2-5 ohm,\npower rated) or a device\nto power in OUTPUT mode", loc="bottom", fontsize=9))
        q1 = d.add(elm.NFet().right().reverse().anchor("drain").at(rl.end))
        text(d, (LX + 0.5, q1.gate[1] + 0.2), "Q1  IRLZ44N / AO3400\nlogic-level N-MOSFET", 9)
        d.add(elm.Line().at(q1.source).toy(0).linewidth(3))
        d.add(elm.Line().at((0, 0)).to((LX, 0)).linewidth(3))
        text(d, (6, -0.4), "GND return (heavy wire)", 8, color=GREY)

        # gate drive
        d.add(elm.Line().at(q1.gate).left(0.8))
        d.add(elm.Dot())
        g = d.here
        r3 = d.add(elm.Resistor().left().label("R3  100", loc="top", fontsize=9))
        net(d, r3.end, "LOAD_EN", "left", 0.3)
        d.add(elm.Resistor().at(g).down().label("R4\n100k", loc="bottom", fontsize=9))
        d.add(elm.Ground())

        # ---------------- power for the electronics, from the cell under test
        net(d, (PX, 15), "BAT+", "left", 0.4)
        sw = d.add(elm.Switch().at((PX, 15)).right().label("SW1  power", loc="top", fontsize=9))
        net(d, sw.end, "BAT_SW", "right", 0.4)
        ic(d, (PX + 3, 7), 4,
           [("VIN+", "BAT_SW"), ("VIN-", "GND")],
           [("VOUT+", "+5V"), ("VOUT-", "GND")],
           "U5  5 V boost module\n(MT3608 / similar)")
        ic(d, (PX + 3, 0), 4,
           [("IN", "+5V"), ("GND", "GND")],
           [("OUT", "+3V3")],
           "U6  3.3 V regulator\n(AMS1117-3.3 / HT7333,\n10 uF in + out)")

        # ---------------- charger (external USB 5 V, charging only)
        VTOP = 19
        d.add(elm.Vdd().at((CX, VTOP)).label("USB 5 V in  (charging only, 1 A)", fontsize=10))
        d.add(elm.Line().at((CX, VTOP)).down(1.0))
        d.add(elm.Dot())
        top = d.here
        d.add(elm.Line().down(2.0))
        q2 = d.add(elm.PFet().right().reverse().anchor("source"))
        text(d, (CX + 0.5, q2.gate[1] + 0.2), "Q2  AO3401\nP-MOSFET", 9)
        d.add(elm.Line().at(q2.gate).left(0.8))
        d.add(elm.Dot())
        pg = d.here
        d.add(elm.Line().at(top).tox(pg[0]))
        d.add(elm.Resistor().to(pg).label("R5 10k", loc="bottom", fontsize=9))
        d.add(elm.Line().at(pg).down(1.0))
        q3 = d.add(elm.BjtNpn(circle=True).right().anchor("collector"))
        d.add(elm.Ground().at(q3.emitter))
        text(d, (pg[0] + 0.9, q3.base[1] + 0.3), "Q3  2N3904", 9)
        d.add(elm.Line().at(q3.base).left(0.6))
        d.add(elm.Dot())
        bb = d.here
        r6 = d.add(elm.Resistor().left().label("R6  1k", loc="top", fontsize=9))
        net(d, r6.end, "CHG_EN", "left", 0.3)
        d.add(elm.Resistor().at(bb).down().label("R7\n100k", loc="bottom", fontsize=9))
        d.add(elm.Ground())

        tp = d.add(elm.Ic(pins=[
            elm.IcPin(name="IN-", side="left", anchorname="INN", slot="1/2"),
            elm.IcPin(name="IN+", side="left", anchorname="INP", slot="2/2"),
            elm.IcPin(name="STDBY", side="right", anchorname="STDBY", slot="1/4"),
            elm.IcPin(name="CHRG", side="right", anchorname="CHRG", slot="2/4"),
            elm.IcPin(name="BAT-", side="right", anchorname="BN", slot="3/4"),
            elm.IcPin(name="BAT+", side="right", anchorname="BP", slot="4/4"),
        ], w=4, pinspacing=1.0, edgepadH=0.7).right().anchor("INP").at((CX + 1.5, 9))
            .label("U4  TP4056 module\n(1 A, use version\nwithout DW01 protection)", "top", fontsize=9))
        d.add(elm.Line().at(q2.drain).toy(tp.INP[1]))
        d.add(elm.Line().tox(tp.INP[0]))
        d.add(elm.Line().at(tp.INN).left(0.6))
        d.add(elm.Line().down(0.6))
        d.add(elm.Ground())
        net(d, tp.BP, "BAT+", "right", 0.8)
        net(d, tp.BN, "GND", "right", 0.8)
        r8 = d.add(elm.Resistor().at(tp.CHRG).right(4.5).label("R8 10k", loc="top", fontsize=9))
        net(d, r8.end, "CHG_CHRG", "right", 0.3)
        r9 = d.add(elm.Resistor().at(tp.STDBY).right(4.5).label("R9 10k", loc="bottom", fontsize=9))
        net(d, r9.end, "CHG_STDBY", "right", 0.3)
        text(d, (CX + 1.5, tp.INN[1] - 2.4),
             "CHRG / STDBY = TP4056 IC pins 7 / 6\n(LED cathode side, open-drain, LOW = active)\n"
             "BAT- to GND (not the cell): charge current\nthen flows through Rs and is measured too", 8, color=GREY)

        # ---------------- notes
        text(d, (-2, -4),
             "Notes\n"
             "- Rs sits in the cell minus lead, so ALL cell current is measured: load + electronics (boost) + charging.\n"
             "- STAR GND is the GND end of Rs. Q1 source, U5/U6, AD7705 module GND and TP4056 BAT- all join there.\n"
             "- SHUNT- (cell minus) goes to AD7705 AIN1+ on its own thin wire; the module GND wire is the other sense lead.\n"
             "- The AD7705 module's 10k/10k dividers halve both inputs: the voltage wire needs no external divider.\n"
             "- Shunt channel: bipolar, gain 64, sign inverted in firmware; up to 2.5 A (-25 mV at the chip, limit -30 mV).\n"
             "- The electronics draw ~30-60 mA from the cell. Switch SW1 off after a test, or the cell keeps draining.\n"
             "- Programming: do not connect the FTDI adapter VCC while the analyzer runs from the cell.\n"
             "- R4 / R7 keep the load and charger off while the MCU resets or is being programmed.\n"
             "- No temperature sensing: do not leave high-current tests unattended.", 9)
        title_block(d, (CX, -4), 2, "Power, load, shunt, charger")
    return path


if __name__ == "__main__":
    p1 = sheet1(os.path.join(HERE, "_s1.pdf"))
    p2 = sheet2(os.path.join(HERE, "_s2.pdf"))
    w = PdfWriter()
    for p in (p1, p2):
        w.append(p)
    w.add_metadata({"/Title": "Li-ion Battery Analyzer - Schematic"})
    with open(OUT, "wb") as f:
        w.write(f)
    for p in (p1, p2):
        os.remove(p)
    print("wrote", OUT)
