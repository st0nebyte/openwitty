// Usage (headless): -postScript StringXrefs.java <substring> [decompile=0|1]
// Lists defined strings containing <substring>, their references and containing functions.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;

public class StringXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String needle = a.length > 0 ? a[0] : "TUSBAUDIO_AudioControlRequest";
        boolean dec = a.length > 1 && a[1].equals("1");
        DecompInterface di = null;
        if (dec) { di = new DecompInterface(); di.openProgram(currentProgram); }
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (!d.hasStringValue()) continue;
            String v = String.valueOf(d.getValue());
            if (!v.contains(needle)) continue;
            println("STRING " + d.getAddress() + " \"" + v + "\"");
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = getFunctionContaining(r.getFromAddress());
                println("  REF " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
                if (dec && f != null) {
                    DecompileResults res = di.decompileFunction(f, 60, monitor);
                    if (res.decompileCompleted()) println(res.getDecompiledFunction().getC());
                }
            }
        }
    }
}
