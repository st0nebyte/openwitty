import lief,sys,re,capstone
path=sys.argv[1]; pat=re.compile(sys.argv[2]); maxn=int(sys.argv[3]) if len(sys.argv)>3 else 40
fat=lief.MachO.parse(path); b=fat.take(lief.MachO.Header.CPU_TYPE.X86_64)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
syms=sorted([(s.value,s.name) for s in b.symbols if s.value and s.name])
starts=[v for v,_ in syms]
import bisect
for addr,name in syms:
    if not pat.search(name): continue
    i=bisect.bisect_right(starts,addr); end=starts[i] if i<len(starts) else addr+64
    end=min(end,addr+maxn*8)
    try: code=bytes(b.get_content_from_virtual_address(addr,end-addr))
    except Exception as e: print(name,'ERR',e); continue
    print("==",name,hex(addr))
    for ins in md.disasm(code,addr): print("  %x: %s %s"%(ins.address,ins.mnemonic,ins.op_str))
