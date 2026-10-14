import lief,sys
fat=lief.MachO.parse(sys.argv[1])
b=fat.take(lief.MachO.Header.CPU_TYPE.X86_64)
print("symbols",len(b.symbols))
names=[s.name for s in b.symbols if s.name]
import re
for n in names:
    if re.search(r'Connect2|LocalUSB|Thesycon|UsbDev|ControlTransfer|PackedStruct',n) and 'function' not in n: print(n)
