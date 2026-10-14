// Usage: -postScript IndirectCalls.java <hexoffset> [decompile=1]
// Finds `call [reg + offset]` instructions (function-pointer-table calls) and decompiles callers.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import java.util.*;

public class IndirectCalls extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String off = a[0].toLowerCase();
        boolean dec = a.length < 2 || a[1].equals("1");
        long needScalar = a.length >= 3 ? Long.decode(a[2]) : -1;
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        Set<Function> seen = new LinkedHashSet<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (!i.getMnemonicString().equals("CALL")) continue;
            String s = i.toString().toLowerCase();
            if (s.contains("+ 0x" + off + "]") || s.contains("+0x" + off + "]")) {
                Function f = getFunctionContaining(i.getAddress());
                if (needScalar < 0) println("CALL " + i.getAddress() + "  " + i + "  in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
                if (f != null) {
                    boolean ok = needScalar < 0;
                    if (!ok) {
                        InstructionIterator fi = currentProgram.getListing().getInstructions(f.getBody(), true);
                        while (fi.hasNext() && !ok) {
                            Instruction j = fi.next();
                            for (int k = 0; k < j.getNumOperands(); k++) {
                                ghidra.program.model.scalar.Scalar sc = j.getScalar(k);
                                if (sc != null && sc.getUnsignedValue() == needScalar) { ok = true; break; }
                            }
                        }
                    }
                    if (ok) seen.add(f);
                }
            }
        }
        for (Function f : seen) println("MATCH " + f.getName() + " @ " + f.getEntryPoint());
        if (dec) for (Function f : seen) {
            DecompileResults r = di.decompileFunction(f, 90, monitor);
            println("==== " + f.getName() + " @ " + f.getEntryPoint());
            println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "decompile failed");
        }
    }
}
