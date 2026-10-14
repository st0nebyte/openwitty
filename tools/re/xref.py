"""List functions that take the address of a symbol (lea/mov rip-relative) -- usage: xref.py BIN SYMREGEX"""
import lief,sys,re,capstone,bisect,struct
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
S={s.value:s.name for s in b.symbols if s.value and s.name}; st=sorted(S)
targets={s.value for s in b.symbols if s.value and re.search(sys.argv[2],s.name)}
tgt_all=set()
for t in targets: tgt_all|={t,t+0x10}
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
txt=[s for s in b.sections if s.name=='__text'][0]
code=bytes(txt.content); base=txt.virtual_address
hits={}
for i in md.disasm(code,base):
    m=re.search(r'rip ([+-]) (0x[0-9a-f]+)',i.op_str)
    if not m: continue
    t=i.address+i.size+int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
    if t in tgt_all:
        k=bisect.bisect_right(st,i.address)-1
        hits.setdefault(S[st[k]],[]).append(hex(i.address))
for f,a in hits.items(): print(f[:150],a[:3])
