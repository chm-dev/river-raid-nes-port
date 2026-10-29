"""Compare tests/host harness output with tools/river_model.py."""
import sys
import pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools'))
from river_model import initial_state
lines=open(sys.argv[1]).read().split("\n")
st=initial_state(0)
st.nes_edges = True
exp={}
N=0
for ln in lines:
    if not ln.strip(): continue
    head,rowhex=ln.split("|")
    h=head.split(); d=int(h[0]); state=list(map(int,h[1:17])); row=bytes.fromhex(rowhex.strip())
    t=d-1
    # advance model to raw row 8t+7
    while N<=8*t+7:
        r=bytes(st.step()); N+=1
    mstate=[st.left,st.width,st.phase,st.rowcount,st.island_flags,st.feature,st.level,st.lfsr_a,st.lfsr_b,st.lfsr_c,st.countdown,st.flags23]
    if row!=r[:48] or state[:12]!=mstate:
        print("MISMATCH at tile row",t,"level",mstate[6]);print(" C  state",state[:12]);print(" py state",mstate);
        print(" len",len(row),len(r)); print(" C ",row.hex()); print(" py",r[:48].hex()); break
else:
    print("all",len(lines)-1,"rows match")

