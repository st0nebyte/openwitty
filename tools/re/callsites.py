"""callsites.py BIN SYMREGEX [N] -- show N instructions preceding each direct call to matching symbols (x86_64 Mach-O)"""
import lief,sys,re,capstone,bisect
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
n=int(sys.argv[3]) if len(sys.argv)>3 else 6
S={s.value:s.name for s in b.symbols if s.value and s.name}; st=sorted(S)
tg={v for v,nm in S.items() if re.search(sys.argv[2],nm)}
txt=[s for s in b.sections if s.name=='__text'][0]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.skipdata=True
ins=list(md.disasm(bytes(txt.content),txt.virtual_address))
for k,i in enumerate(ins):
    if i.mnemonic=='call':
        try: t=int(i.op_str,16)
        except: continue
        if t in tg:
            fk=bisect.bisect_right(st,i.address)-1
            print("==",hex(i.address),"in",S[st[fk]][:80])
            for j in ins[max(0,k-n):k+1]: print("   %x %s %s"%(j.address,j.mnemonic,j.op_str))
