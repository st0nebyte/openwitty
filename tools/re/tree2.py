import lief,sys,re,capstone,bisect
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
S={s.value:s.name for s in b.symbols if s.value and s.name}
a=[v for v,n in S.items() if n=='__ZN6lewitt4tree17buildConnect2TreeEPN3LWT8Connect214Connect2DeviceE'][0]
code=bytes(b.get_content_from_virtual_address(a,48832))
def rip(ins):
    m=re.search(r'rip ([+-]) (0x[0-9a-f]+)',ins.op_str)
    return ins.address+ins.size+int(m.group(2),16)*(1 if m.group(1)=='+' else -1) if m else None
ev=[]
for ins in md.disasm(code,a):
    t=rip(ins)
    if t is None: continue
    if ins.mnemonic=='mov' and ins.op_str.startswith(('rsi','rdx','rcx','rax','r8','rdi','r9')) and '[rip' in ins.op_str:
        n=S.get(t)
        if n and '$_' not in n: ev.append(('ID',ins.address,n))
    elif ins.mnemonic=='lea':
        n=S.get(t) or S.get(t-0x10)
        if n and 'buildConnect2Tree' in n:
            m=re.search(r'\$_(\d+)',n); ev.append(('LAM',ins.address,m.group(1) if m else n))
    # also 'call' to Value ctor / Group
for ins in md.disasm(code,a):
    if ins.mnemonic=='call':
        try: t=int(ins.op_str,16)
        except: continue
        n=S.get(t,'')
        if 'PropertyItemDefinition' in n: ev.append(('PROP',ins.address,'itemdef'))
        elif n.startswith('__ZN6lewitt4tree5GroupC2'): ev.append(('GROUP',ins.address,'group'))
ev.sort(key=lambda e:e[1])
for k,ad,n in ev: print("%x %-5s %s"%(ad,k,n[:120]))
