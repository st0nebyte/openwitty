"""strxref.py BIN SUBSTR -- list functions whose code takes the address of a C string containing SUBSTR (x86_64 Mach-O)."""
import lief,sys,re,capstone,bisect
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
needle=sys.argv[2].encode()
S={s.value:s.name for s in b.symbols if s.value and s.name}; st=sorted(S)
strs={}
for sec in b.sections:
    if sec.name in('__cstring','__const','__data'):
        data=bytes(sec.content); base=sec.virtual_address
        for m in re.finditer(rb'[\x20-\x7e\n]{3,}\x00',data):
            if needle in m.group(0): strs[base+m.start()]=m.group(0)[:-1].decode(errors='replace')
txt=[s for s in b.sections if s.name=='__text'][0]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.skipdata=True
for i in md.disasm(bytes(txt.content),txt.virtual_address):
    m=re.search(r'rip ([+-]) (0x[0-9a-f]+)',i.op_str)
    if not m: continue
    t=i.address+i.size+int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
    if t in strs:
        k=bisect.bisect_right(st,i.address)-1
        print(hex(i.address),S[st[k]][:100],repr(strs[t][:60]))
