import lief,sys,struct,re
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
sym={s.value:s.name for s in b.symbols if s.value}
p=re.compile(sys.argv[2])
for s in b.symbols:
    if s.value and p.search(s.name):
        data=bytes(b.get_content_from_virtual_address(s.value,8*14))
        print("==",s.name)
        for i in range(0,len(data),8):
            v=struct.unpack_from('<Q',data,i)[0]
            print("  +%#x"%i, hex(v), sym.get(v,'?')[:150])
