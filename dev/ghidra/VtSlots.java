// VtSlots <vt hex> <n> <out> — слоты vtable по адресу и декомпиляция каждого (уникальные)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.util.*;

public class VtSlots extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address vt = toAddr(Long.parseLong(a[0], 16));
        int n = Integer.parseInt(a[1]);
        PrintWriter out = new PrintWriter(a[2]);
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        Set<Address> done = new HashSet<>();
        for (int i = 0; i < n; i++) {
            Address fa = toAddr(getInt(vt.add(i * 4)) & 0xffffffffL);
            Function f = getFunctionAt(fa);
            if (f == null) { try { disassemble(fa); f = createFunction(fa, null); } catch (Exception e) {} }
            out.println("---- slot" + i + " -> " + fa + (f == null ? "" : " " + f.getName()));
            if (f == null || !done.add(fa)) continue;
            DecompileResults r = dec.decompileFunction(f, 60, monitor);
            if (r != null && r.decompileCompleted()) {
                String c = r.getDecompiledFunction().getC();
                out.println(c.length() > 4000 ? c.substring(0, 4000) + "\n...[cut]" : c);
            }
        }
        out.close();
    }
}
