// Usage: -postScript CallersOf.java <hexaddr> -> lists call/ref sites to function at address
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class CallersOf extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String s : getScriptArgs()) {
            Address ad = toAddr(s);
            println("== refs to " + ad);
            for (Reference r : getReferencesTo(ad)) {
                Function f = getFunctionContaining(r.getFromAddress());
                println("  " + r.getReferenceType() + " " + r.getFromAddress() + " in " +
                        (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
            }
        }
    }
}
