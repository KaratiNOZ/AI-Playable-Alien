// EntImpl <Class+Class...> <out> — реализация сущности CATHODE по RTTI: «Class::vftable» -> функция, что его ставит
// (конструктор базы) -> её вызывающие (обёртки, ставят итоговые vtable) -> декомпиляция обёрток и тех слотов итоговых
// vtable, которые не общие (встречаются меньше чем в 20 vtable). Для быстрых обзоров многих сущностей.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;
import java.util.regex.*;

public class EntImpl extends GhidraScript {
    DecompInterface dec; PrintWriter out;
    String decomp(Function f) {
        DecompileResults r = dec.decompileFunction(f, 60, monitor);
        return (r != null && r.decompileCompleted()) ? r.getDecompiledFunction().getC() : "(no decomp)";
    }
    Function fnAt(long a) {
        Address ad = toAddr(a);
        Function f = getFunctionAt(ad);
        if (f == null) { try { createFunction(ad, null); } catch (Exception e) {} f = getFunctionAt(ad); }
        return f;
    }
    @Override
    protected void run() throws Exception {
        String[] names = getScriptArgs()[0].split("\\+");
        out = new PrintWriter(getScriptArgs()[1]);
        dec = new DecompInterface(); dec.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        SymbolTable st = currentProgram.getSymbolTable();
        for (String nm : names) {
            out.println("\n########## " + nm);
            Address vt = null;
            for (Symbol s : st.getSymbols("vftable")) {
                if (s.getName(true).equals("CATHODE::" + nm + "::vftable")) { vt = s.getAddress(); break; }
            }
            if (vt == null) { out.println("no vftable"); continue; }
            out.println("base vftable " + vt);
            Set<Function> ctors = new LinkedHashSet<>();
            for (Reference r : getReferencesTo(vt)) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) ctors.add(f);
            }
            for (Function c : ctors) {
                out.println("-- base ctor " + c.getName() + " @ " + c.getEntryPoint());
                for (Function w : c.getCallingFunctions(monitor)) {
                    String code = decomp(w);
                    out.println("---- wrapper " + w.getName() + " @ " + w.getEntryPoint());
                    out.println(code);
                    Matcher m = Pattern.compile("PTR_(?:FUN|LAB)_([0-9a-f]{8})|&PTR_([0-9a-f]{8})|PTR_[A-Za-z_]*_([0-9a-f]{8})").matcher(code);
                    Set<Long> vts = new LinkedHashSet<>();
                    Matcher m2 = Pattern.compile("&PTR_[A-Za-z_]*?_?([0-9a-f]{8})").matcher(code);
                    while (m2.find()) vts.add(Long.parseLong(m2.group(1), 16));
                    for (long v : vts) {
                        out.println("====== derived vtable " + Long.toHexString(v));
                        for (int i = 0; i < 40; i++) {
                            long fa;
                            try { fa = mem.getInt(toAddr(v + 4L * i)) & 0xffffffffL; } catch (Exception e) { break; }
                            if (fa < 0x401000L || fa > 0x1200000L) break;
                            int uses = 0;
                            for (Reference r : getReferencesTo(toAddr(fa))) { if (r.getReferenceType().isData()) uses++; }
                            out.println("  slot" + i + " -> " + Long.toHexString(fa) + "  (data refs " + uses + ")");
                            if (uses < 20) {
                                Function f = fnAt(fa);
                                if (f != null) out.println(decomp(f));
                            }
                        }
                    }
                }
            }
            out.flush();
        }
        out.close();
    }
}
