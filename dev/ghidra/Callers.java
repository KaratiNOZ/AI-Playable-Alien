// Callers <hex+hex...> <out> — вызывающие функции (с декомпиляцией)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.util.*;

public class Callers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        Set<Address> done = new HashSet<>();
        for (String h : getScriptArgs()[0].split("\\+")) {
            Function f = getFunctionAt(toAddr(Long.parseLong(h, 16)));
            out.println("\n##### callers of " + h);
            if (f == null) continue;
            for (Function c : f.getCallingFunctions(monitor)) {
                out.println("---- " + c.getName() + " @ " + c.getEntryPoint());
                if (!done.add(c.getEntryPoint())) continue;
                DecompileResults r = dec.decompileFunction(c, 120, monitor);
                if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
            }
        }
        out.close();
    }
}
