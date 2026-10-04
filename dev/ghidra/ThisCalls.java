// ThisCalls <global hex> <out> — функции, которые зовут с ECX = *(global); декомпиляция тех, что пишут в this+4
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;

public class ThisCalls extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address g = toAddr(Long.parseLong(a[0], 16));
        PrintWriter out = new PrintWriter(a[1]);
        Listing L = currentProgram.getListing();
        Map<Address, List<Address>> callees = new TreeMap<>();
        for (Reference r : getReferencesTo(g)) {
            Instruction ins = L.getInstructionAt(r.getFromAddress());
            if (ins == null || !ins.getMnemonicString().equals("MOV")) continue;
            if (!ins.getDefaultOperandRepresentation(0).equals("ECX")) continue;
            Instruction cur = ins;
            for (int i = 0; i < 12 && cur != null; i++) {
                cur = cur.getNext();
                if (cur == null) break;
                String m = cur.getMnemonicString();
                if (m.equals("CALL")) {
                    Address[] fl = cur.getFlows();
                    if (fl.length > 0) callees.computeIfAbsent(fl[0], k -> new ArrayList<>()).add(cur.getAddress());
                    break;
                }
                if (cur.getNumOperands() > 0 && cur.getDefaultOperandRepresentation(0).equals("ECX")) break;
            }
        }
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        for (Map.Entry<Address, List<Address>> e : callees.entrySet()) {
            Function f = getFunctionAt(e.getKey());
            out.println("---- " + e.getKey() + " " + (f == null ? "?" : f.getName()) + " called from " + e.getValue());
            if (f == null) continue;
            DecompileResults d = dec.decompileFunction(f, 60, monitor);
            if (d == null || !d.decompileCompleted()) continue;
            String c = d.getDecompiledFunction().getC();
            if (c.contains("param_1 + 4) =") || c.contains("param_1[1] =") || c.contains("param_1 + 4) |") || c.length() < 1500) out.println(c);
        }
        out.close();
    }
}
