// Usage: -postScript CtorCallers.java <class-namespace-substring>
// For vftables in matching namespaces: functions that write the vftable (constructors) and, for each,
// every call site with the preceding instructions (to read immediate constructor arguments).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class CtorCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String needle = getScriptArgs()[0];
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        Set<Address> ctors = new LinkedHashSet<>();
        while (it.hasNext()) {
            Symbol s = it.next();
            if (!s.getName().contains("vftable")) continue;
            String ns = s.getParentNamespace().getName(true);
            if (!ns.contains(needle)) continue;
            println("VFTABLE " + s.getAddress() + " ns=" + ns);
            for (Reference r : getReferencesTo(s.getAddress())) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) { ctors.add(f.getEntryPoint()); println("  WRITER " + r.getFromAddress() + " in " + f.getName() + "@" + f.getEntryPoint()); }
            }
        }
        for (Address c : ctors) {
            println("CTOR " + c);
            for (Reference r : getReferencesTo(c)) {
                Function f = getFunctionContaining(r.getFromAddress());
                println("  CALL " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
                Instruction ins = getInstructionAt(r.getFromAddress());
                for (int k = 0; k < 6 && ins != null; k++) { ins = ins.getPrevious(); }
                for (int k = 0; k < 7 && ins != null; k++) { println("      " + ins.getAddress() + " " + ins); ins = ins.getNext(); }
            }
        }
    }
}
