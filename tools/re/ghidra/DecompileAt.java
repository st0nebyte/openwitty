// Usage: -postScript DecompileAt.java <hexaddr> [more addrs...]   (decompiles containing function)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (String s : getScriptArgs()) {
            Address ad = toAddr(s);
            Function f = getFunctionContaining(ad);
            if (f == null) { println("NOFUNC " + s); continue; }
            println("==== " + f.getName() + " @ " + f.getEntryPoint() + " size " + f.getBody().getNumAddresses());
            DecompileResults r = di.decompileFunction(f, 90, monitor);
            println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "decompile failed: " + r.getErrorMessage());
        }
    }
}
