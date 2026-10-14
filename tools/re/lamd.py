import lief,sys,re,capstone,bisect
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
S={s.value:s.name for s in b.symbols if s.value and s.name}
starts=sorted(S)
want=set(sys.argv[2].split(','))
for a in starts:
    n=S[a]
    m=re.search(r'buildConnect2Tree.*?\$_(\d+)NS_9allocator',n)
    if not m or m.group(1) not in want : continue
    if 'clE' not in n or '__clone' in n: continue
    end=starts[bisect.bisect_right(starts,a)]
    print("== $_"+m.group(1),n[-30:])
    for i in md.disasm(bytes(b.get_content_from_virtual_address(a,end-a)),a):
        if i.mnemonic in('nop','push','pop') or i.op_str=='rbp, rsp': continue
        print("  %s %s"%(i.mnemonic,i.op_str))
