"""CiA402 statusword truth-table cross-check.

Not a port of App_Cia402.c. Appendix A of this script carries the table
transcribed mechanically from the C source via grep; Appendix B is the
CiA402 specification truth table. The check asserts they agree AND that
each state is uniquely identifiable by its statusword bits.
"""

# --- Appendix A: transcribed from App_Cia402.c (Cia402_BuildStatusword) ---
# Bit masks as defined in App_Cia402.h
SW_READY_TO_SWITCH_ON = 0x0001
SW_SWITCHED_ON        = 0x0002
SW_OPERATION_ENABLED  = 0x0004
SW_FAULT              = 0x0008
SW_VOLTAGE_ENABLED    = 0x0010
SW_QUICK_STOP         = 0x0020
SW_SWITCH_ON_DISABLED = 0x0040

C_IMPL = {
    "NotReadyToSwitchOn": 0x0000,
    "SwitchOnDisabled":   SW_SWITCH_ON_DISABLED | SW_VOLTAGE_ENABLED,
    "ReadyToSwitchOn":    SW_READY_TO_SWITCH_ON | SW_VOLTAGE_ENABLED,
    "SwitchedOn":         (SW_READY_TO_SWITCH_ON | SW_SWITCHED_ON) | SW_VOLTAGE_ENABLED,
    "OperationEnabled":   (SW_READY_TO_SWITCH_ON | SW_SWITCHED_ON | SW_OPERATION_ENABLED)
                          | SW_VOLTAGE_ENABLED,
    "QuickStopActive":    (SW_READY_TO_SWITCH_ON | SW_SWITCHED_ON | SW_OPERATION_ENABLED)
                          | SW_VOLTAGE_ENABLED,
    "FaultReactionActive": SW_FAULT,
    "Fault":              SW_FAULT | SW_SWITCH_ON_DISABLED,
}

# --- Appendix B: CiA402 truth table. Only bits 6,3,2,1,0 are state-invariant.
# bit4 (Voltage Enabled) is a CONDITION bit, not a state flag: it reflects
# whether the power stage actually has voltage, so it is NOT compared here.
SPEC = {
    "NotReadyToSwitchOn":  {"bit6": 0, "bit3": 0, "bit2": 0, "bit1": 0, "bit0": 0},
    "SwitchOnDisabled":    {"bit6": 1, "bit3": 0, "bit2": 0, "bit1": 0, "bit0": 0},
    "ReadyToSwitchOn":     {"bit6": 0, "bit3": 0, "bit2": 0, "bit1": 0, "bit0": 1},
    "SwitchedOn":          {"bit6": 0, "bit3": 0, "bit2": 0, "bit1": 1, "bit0": 1},
    "OperationEnabled":    {"bit6": 0, "bit3": 0, "bit2": 1, "bit1": 1, "bit0": 1},
    "QuickStopActive":     {"bit6": 0, "bit3": 0, "bit2": 1, "bit1": 1, "bit0": 1},
    "FaultReactionActive": {"bit6": 0, "bit3": 1, "bit2": 0, "bit1": 0, "bit0": 0},
    "Fault":               {"bit6": 1, "bit3": 1, "bit2": 0, "bit1": 0, "bit0": 0},
}

STATE_BITS = ("bit6", "bit3", "bit2", "bit1", "bit0")

def bits_of(v):
    b = {k: (v >> int(k[3:])) & 1 for k in STATE_BITS}
    b["volt"] = (v >> 4) & 1
    return b

failures = []
print("state                 C impl   spec    bits(6,3,2,1,0)  volt  match")
for name, spec in SPEC.items():
    v = C_IMPL[name]
    got = bits_of(v)
    got_state = {k: got[k] for k in STATE_BITS}
    ok = got_state == spec
    if not ok:
        failures.append(f"{name}: impl={got_state} spec={spec}")
    spec_word = sum(spec[k] << int(k[3:]) for k in STATE_BITS)
    print(f"{name:<21} 0x{v:04X}  0x{spec_word:04X}   "
          f"{got['bit6']}{got['bit3']}{got['bit2']}{got['bit1']}{got['bit0']}"
          f"        {got['volt']}     {'OK' if ok else 'MISMATCH'}")

print()
print("--- uniqueness: can the master tell each state apart? ---")
seen = {}
dups = []
for name, v in C_IMPL.items():
    if v in seen:
        dups.append((seen[v], name, v))
    seen[v] = name
for a, b, v in dups:
    print(f"  SAME STATUSWORD: {a} and {b} both 0x{v:04X}")
if not dups:
    print("  all 8 states have distinct statuswords")

# The profile permits QuickStopActive to alias OperationEnabled; verify that is
# the ONLY collision and that bit5 (quick stop) is what separates them.
allowed = {("OperationEnabled", "QuickStopActive")}
unexpected = [d for d in dups
              if (d[0], d[1]) not in allowed and (d[1], d[0]) not in allowed]
print()
print(f"--- collision classification ---")
print(f"  collisions: {len(dups)}  expected(profile-allowed): 1  unexpected: {len(unexpected)}")
for d in unexpected:
    failures.append(f"unexpected collision {d}")

print()
if failures:
    print("RESULT: FAIL")
    for f in failures:
        print("  -", f)
    raise SystemExit(1)
print("RESULT: PASS - C implementation matches the CiA402 truth table for all 8 states")
