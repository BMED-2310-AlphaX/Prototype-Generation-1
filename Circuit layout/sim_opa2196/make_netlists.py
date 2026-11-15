"""Write LTspice netlists comparing NE5532 (UniversalOpAmp2, datasheet params) and
OPA2196 (TI PSpice model SBOMAF9E) in the as-built KiCad stimulator."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OPA_LIB = "OPAx196.LIB"   # TI SBOMAF9 model, placed next to the netlists (see README.md)

NE = ("level2 Avol=100k GBW=10Meg Slew=9Meg Ilimit=38m Rail=2 Vos=0.5m "
      "En=0 Enk=0 In=0 Ink=0 Rin=500Meg")


def amp(name, a, b, vp, vn, o, kind):
    if kind == "ne":
        return f"X{name} {a} {b} {vp} {vn} {o} {NE}\n"
    return f"X{name} {a} {b} {vp} {vn} {o} OPAx196\n"


def copy(s, kind):
    """One stimulator + body model. s = node suffix."""
    vp, vn = f"vp_{s}", f"vn_{s}"
    return (
        f"* ---- {s}: {'NE5532 (UniversalOpAmp2)' if kind == 'ne' else 'OPA2196 (TI model)'}\n"
        f"VPOS_{s} {vp} 0 14.56\nVNEG_{s} {vn} 0 -14.68\n"
        f"R1_{s} inn_{s} dref {{R1v}}\nR2_{s} out_{s} inn_{s} {{R2v}}\n"
        f"R3_{s} inp_{s} dst {{R3v}}\nR4_{s} drv_{s} inp_{s} {{R4v}}\n"
        f"RS_{s} drv_{s} out_{s} 100\nRBLEED_{s} drv_{s} 0 150k\n"
        f"CBLK_{s} drv_{s} eblk_{s} 2.2u\nRPMOS_{s} eblk_{s} elec_{s} 50\n"
        f"RSK1_{s} elec_{s} b1_{s} {{Rskin}}\nCSK1_{s} elec_{s} b1_{s} {{Cskin}}\n"
        f"RBODY_{s} b1_{s} b2_{s} {{Rtis}}\n"
        f"RSK2_{s} b2_{s} 0 {{Rskin}}\nCSK2_{s} b2_{s} 0 {{Cskin}}\n"
        f"RI_{s} sn_{s} drv_{s} 1Meg\nCI_{s} vs_{s} sn_{s} 1u\nRP_{s} sp_{s} 0 1Meg\n"
        f"RINJ_{s} vs_{s} inp_{s} 10Meg\nRINJ2_{s} inn_{s} 0 10Meg\n"
        + amp(f"A_{s}", f"inp_{s}", f"inn_{s}", vp, vn, f"out_{s}", kind)
        + amp(f"B_{s}", f"sp_{s}", f"sn_{s}", vp, vn, f"vs_{s}", kind)
    )


HEAD = """* {title}
* DAC: DAC_REF held at 1.5 V; DAC_STIM = 1.5 V + biphasic pulse
VDREF dref 0 1.5
VP1 dmid dref PULSE(0 {{Vset}} 1m 1u 1u {{PW}} {{1/Freq}})
VP2 dst dmid PULSE(0 {{-Vset}} {{1m+PW+IPG}} 1u 1u {{PW}} {{1/Freq}})
.param Freq=25 PW=250u IPG=50u Rsense=100
.param Vset={{Iamp/(1/Rsense+1/150k)}}
.param Cskin=470n Rtis=500
* bridge tolerance corner: mm>0 pushes R2*R3 above R1*(R4+Rs) (toward negative output impedance)
.param R1v={{150k*(1-mm)}} R2v={{150k*(1+mm)}} R3v={{150k*(1+mm)}} R4v={{150k*(1-mm)}}
.lib UniversalOpAmp2.lib
.include "{lib}"
"""

runs = {
    "cmp_amplitude": dict(
        title="NE5532 vs OPA2196: amplitude sweep, nominal parts, Rskin 10k",
        params=".param Rskin=10k mm=0\n.step param Iamp list 2m 5m 7.5m 10m\n"),
    "cmp_corners": dict(
        title="NE5532 vs OPA2196: 5 mA, bridge tolerance corners x skin impedance",
        params=".param Iamp=5m\n.step param mm list -0.0005 0 0.0005\n.step param Rskin list 10k 50k\n"),
}
for name, r in runs.items():
    text = (HEAD.format(title=r["title"], lib=OPA_LIB) + r["params"] + copy("ne", "ne") + copy("op", "opa")
            + ".tran 0 201m 0 1u\n.options plotwinsize=0\n.backanno\n.end\n")
    open(os.path.join(HERE, name + ".cir"), "w", encoding="utf8", newline="\n").write(text)
    print("wrote", name + ".cir")
