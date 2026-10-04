// Decomp <hex+hex...> <out> [depth] — декомпиляция функций по адресу (+ вызываемых до depth уровней)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.util.*;

public class Decomp extends GhidraScript {
    DecompInterface dec; PrintWriter out; Set<Address> done = new HashSet<>();
    void dump(Address a, int depth) throws Exception {
        if (!done.add(a)) return;
        Function f = getFunctionAt(a);
        if (f == null) { try { disassemble(a); f = createFunction(a, null); } catch (Exception e) {} }
        if (f == null) { out.println("no function at " + a); return; }
        out.println("---- " + f.getName() + " @ " + a + " (callers: " + f.getCallingFunctions(monitor).size() + ")");
        DecompileResults r = dec.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        if (depth > 0) for (Function c : f.getCalledFunctions(monitor)) dump(c.getEntryPoint(), depth - 1);
    }
    @Override
    protected void run() throws Exception {
        out = new PrintWriter(getScriptArgs()[1]);
        int depth = getScriptArgs().length > 2 ? Integer.parseInt(getScriptArgs()[2]) : 0;
        dec = new DecompInterface(); dec.openProgram(currentProgram);
        for (String h : getScriptArgs()[0].split("\\+")) dump(toAddr(Long.parseLong(h, 16)), depth);
        out.close();
    }
}
