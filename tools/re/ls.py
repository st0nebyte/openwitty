import lief,sys,re
b=lief.MachO.parse(sys.argv[1]).take(lief.MachO.Header.CPU_TYPE.X86_64)
p=re.compile(sys.argv[2])
for s in sorted(b.symbols,key=lambda s:s.value):
    if s.value and p.search(s.name): print(hex(s.value),s.name)
