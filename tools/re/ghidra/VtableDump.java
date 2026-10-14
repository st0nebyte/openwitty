// Usage: -postScript VtableDump.java <namespace-substring> [decompile=1]
// Finds vftable symbols in namespaces containing the substring and lists/decompiles slots.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import java.util.*;

public class VtableDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String needle = a[0];
        boolean dec = a.length < 2 || a[1].equals("1");
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            if (!s.getName().contains("vftable")) continue;
            String ns = s.getParentNamespace().getName(true);
            if (!ns.contains(needle)) continue;
            println("VFTABLE " + s.getAddress() + " ns=" + ns);
            Address p = s.getAddress();
            for (int i = 0; i < 40; i++) {
                Address slot = p.add(8L * i);
                long v = currentProgram.getMemory().getLong(slot);
                Address t = toAddr(v);
                Function f = getFunctionAt(t);
                if (f == null) break;
                println("  SLOT " + i + " @+" + Long.toHexString(8L * i) + " -> " + f.getName() + "@" + t +
                        " size " + f.getBody().getNumAddresses());
                if (dec) {
                    DecompileResults r = di.decompileFunction(f, 60, monitor);
                    println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "  decompile failed");
                }
            }
        }
    }
}
