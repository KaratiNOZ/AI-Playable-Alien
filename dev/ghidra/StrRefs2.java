// StrRefs2 <string+string...> <out> [callers:1] — функции, которые ссылаются на строку (+ их вызывающие), с декомпиляцией
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;

public class StrRefs2 extends GhidraScript {
    DecompInterface dec; PrintWriter out; Set<Address> done = new HashSet<>();
    void dump(Function f, String tag) {
        if (f == null || !done.add(f.getEntryPoint())) return;
        out.println("---- " + tag + " " + f.getName() + " @ " + f.getEntryPoint());
        DecompileResults r = dec.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
    }
    @Override
    protected void run() throws Exception {
        String[] strs = getScriptArgs()[0].split("\\+");
        out = new PrintWriter(getScriptArgs()[1]);
        boolean callers = getScriptArgs().length > 2;
        dec = new DecompInterface(); dec.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        for (String s : strs) {
            out.println("\n##### \"" + s + "\"");
            byte[] pat = (s + "\0").getBytes("ASCII");
            Address a = mem.findBytes(mem.getMinAddress(), pat, null, true, monitor);
            while (a != null) {
                out.println("string @ " + a);
                for (int back = 0; back <= 2; back++) for (Reference ref : getReferencesTo(a.subtract(back))) {
                    Function f = getFunctionContaining(ref.getFromAddress());
                    dump(f, "ref from " + ref.getFromAddress());
                    if (callers && f != null)
                        for (Function c : f.getCallingFunctions(monitor)) dump(c, "caller of " + f.getName());
                }
                a = mem.findBytes(a.add(1), pat, null, true, monitor);
            }
        }
        out.close();
    }
}
