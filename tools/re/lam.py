import lief,sys,re,capstone,bisect
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
S={s.value:s.name for s in b.symbols if s.value and s.name}
starts=sorted(S)
def short(n): 
    n=re.sub(r'^__ZN?K?','',n); return n[:110]
out=[]
for a in starts:
    n=S[a]
    m=re.search(r'buildConnect2Tree.*?\$_(\d+)NS_9allocator\w+?(F\w+?)EEcl',n)
    if not m or 'function6__func' not in n or not re.search(r'EEcl\w*E?[vdb]?$',n) or '__clone' in n: continue
    i=bisect.bisect_right(starts,a); end=starts[i]
    code=bytes(b.get_content_from_virtual_address(a,end-a))
    calls=[];disps=set()
    for ins in md.disasm(code,a):
        if ins.mnemonic=='call':
            try:
                t=int(ins.op_str,16); calls.append(short(S.get(t,hex(t))))
            except: calls.append('indirect '+ins.op_str)
        for mm in re.finditer(r'\[(r\w+) \+ (0x[0-9a-f]+)\]',ins.op_str): disps.add((mm.group(1),mm.group(2)))
    print("##",m.group(1),m.group(2),"size",end-a)
    for c in calls: print("   call",c)
    print("   disp",sorted(d for r,d in disps if int(d,16)>=0x80)[:12])
