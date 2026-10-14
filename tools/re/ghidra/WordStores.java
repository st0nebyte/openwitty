// Usage: -postScript WordStores.java <hexoffset> [imm,imm,...]
// Lists `MOV word ptr [reg + offset], imm` (16-bit immediate stores), optionally filtered by value.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.util.*;

public class WordStores extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String off = a[0].toLowerCase();
        Set<Long> want = new HashSet<>();
        if (a.length > 1) for (String t : a[1].split(",")) want.add(Long.decode(t));
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (!i.getMnemonicString().equals("MOV")) continue;
            String s = i.toString().toLowerCase();
            if (!s.contains("word ptr") || s.contains("dword") || s.contains("qword")) continue;
            if (!off.equals("any") && !(s.contains("+ 0x" + off + "]") || s.contains("+0x" + off + "]"))) continue;
            Scalar sc = i.getScalar(1);
            long v = sc == null ? -1 : sc.getUnsignedValue();
            if (!want.isEmpty() && (sc == null || !want.contains(v))) continue;
            Function f = getFunctionContaining(i.getAddress());
            println("STORE " + i.getAddress() + " imm=0x" + Long.toHexString(v) + "  " + i + "  in " +
                    (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
        }
    }
}
