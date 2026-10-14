"""brief.py BIN SYMREGEX -- compact disassembly: calls (non-boilerplate), compares, rip-loaded identifiers/strings."""
import lief,sys,re,capstone,bisect
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
S={s.value:s.name for s in b.symbols if s.value and s.name}; st=sorted(S)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.skipdata=True
cs=[x for x in b.sections if x.name=='__cstring'][0]; cd=bytes(cs.content)
skip=re.compile(r'juce6String|translate|appendChar|StringpL|clang_call|__throw|Colour|Graphics|Font|Rectangle|Justification|ListenerList|shared_ptr|__shared|operator delete|operator new|DeviceRoot|Component[0-9A-Z]|Value[0-9A-Z]')
for a,nm in sorted(S.items()):
    if not re.search(sys.argv[2],nm): continue
    end=st[bisect.bisect_right(st,a)]
    print("==",nm[:120],hex(a),hex(end-a))
    for i in md.disasm(bytes(b.get_content_from_virtual_address(a,end-a)),a):
        e=''
        if i.mnemonic=='call':
            try:e=S.get(int(i.op_str,16),'')
            except: e='indirect'
            if not e or skip.search(e): continue
            print("  %x call %s"%(i.address,e[:100])); continue
        m=re.search(r'rip ([+-]) (0x[0-9a-f]+)',i.op_str)
        if m:
            t=i.address+i.size+int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
            try:p=int.from_bytes(bytes(b.get_content_from_virtual_address(t,8)),'little')
            except:p=0
            n=S.get(t) or S.get(p)
            if n and 'lewitt2id' in n: print("  %x id %s"%(i.address,n[16:]))
            elif cs.virtual_address<=t<cs.virtual_address+len(cd):
                o=t-cs.virtual_address; s=cd[o:cd.index(b'\0',o)][:70]
                if len(s)>3: print("  %x str %r"%(i.address,s.decode(errors='replace')))
            continue
        if i.mnemonic in('cmp','test') and re.search(r'(0x[0-9a-f]+|\d+)$',i.op_str) and 'rsp' not in i.op_str:
            print("  %x %s %s"%(i.address,i.mnemonic,i.op_str))
