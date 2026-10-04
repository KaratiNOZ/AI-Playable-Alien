// VtDiff <baseVtHex> <vtHex+vtHex...> <out> <nSlots> — слоты, которые у класса свои (не как у базового), с декомпиляцией
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class VtDiff extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long base = Long.parseLong(getScriptArgs()[0], 16);
        String[] vts = getScriptArgs()[1].split("\\+");
        PrintWriter out = new PrintWriter(getScriptArgs()[2]);
        int n = Integer.parseInt(getScriptArgs()[3]);
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        for (String v : vts) {
            Address va = toAddr(Long.parseLong(v, 16));
            Symbol[] syms = currentProgram.getSymbolTable().getSymbols(va);
            out.println("\n##### " + va + " " + (syms.length > 0 ? syms[0].getName(true) : ""));
            for (int i = 0; i < n; i++) {
                int e = currentProgram.getMemory().getInt(va.add(i * 4));
                int b = currentProgram.getMemory().getInt(toAddr(base).add(i * 4));
                if (e == b) continue;
                Address fa = toAddr(e & 0xffffffffL);
                if (fa.getOffset() < 0x401000 || fa.getOffset() > 0x1100000) { out.println("  slot" + i + " -> " + fa + " (not code)"); continue; }
                Function f = getFunctionAt(fa);
                if (f == null) { try { disassemble(fa); f = createFunction(fa, null); } catch (Exception ex) {} }
                out.println("---- slot" + i + " -> " + fa);
                if (f == null) continue;
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
            }
        }
        out.close();
    }
}
