// VtOf <ClassName+ClassName...> <out> <slots: 0+1+2...|all:N> — vtable класса по RTTI (символ "Class::vftable") и декомпиляция слотов
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;

public class VtOf extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] names = getScriptArgs()[0].split("\\+");
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        String slotsArg = getScriptArgs()[2];
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        SymbolTable st = currentProgram.getSymbolTable();
        for (String name : names) {
            out.println("\n##### " + name);
            List<Symbol> vts = new ArrayList<>();
            for (Symbol s : st.getAllSymbols(true)) {
                String full = s.getName(true);
                if (s.getName().equals("vftable") && (full.equals(name + "::vftable") || full.endsWith("::" + name + "::vftable"))) vts.add(s);
            }
            if (vts.isEmpty()) { out.println("no vftable symbol"); continue; }
            for (Symbol vs : vts) {
                Address va = vs.getAddress();
                out.println("== " + vs.getName(true) + " @ " + va);
                int n = 0; Set<Integer> want = new HashSet<>();
                if (slotsArg.startsWith("all:")) { n = Integer.parseInt(slotsArg.substring(4)); for (int i = 0; i < n; i++) want.add(i); }
                else { for (String x : slotsArg.split("\\+")) { int i = Integer.parseInt(x); want.add(i); n = Math.max(n, i + 1); } }
                for (int i = 0; i < n; i++) {
                    int e = currentProgram.getMemory().getInt(va.add(i * 4));
                    Address fa = toAddr(e & 0xffffffffL);
                    out.println("  slot" + i + " -> " + fa);
                    if (!want.contains(i)) continue;
                    Function f = getFunctionAt(fa);
                    if (f == null) { try { disassemble(fa); f = createFunction(fa, null); } catch (Exception ex) {} }
                    if (f == null) continue;
                    DecompileResults r = dec.decompileFunction(f, 60, monitor);
                    if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
                }
            }
        }
        out.close();
    }
}
