// DecompForce <hex+hex...> <out> — декомпиляция по адресам; если функции нет, создаёт её (disassemble + createFunction)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.util.*;

public class DecompForce extends GhidraScript {
    @Override
    protected void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        Set<Address> done = new HashSet<>();
        for (String h : getScriptArgs()[0].split("\\+")) {
            Address a = toAddr(Long.parseLong(h, 16));
            Function f = getFunctionAt(a);
            if (f == null) f = getFunctionContaining(a);
            if (f == null) {
                disassemble(a);
                CreateFunctionCmd cmd = new CreateFunctionCmd(a);
                cmd.applyTo(currentProgram, monitor);
                f = getFunctionAt(a);
            }
            if (f == null) { out.println("cannot create function at " + a); continue; }
            out.println("==== " + a + " in " + f.getName() + " @ " + f.getEntryPoint());
            if (!done.add(f.getEntryPoint())) continue;
            StringBuilder cs = new StringBuilder();
            for (Function c : f.getCallingFunctions(monitor)) cs.append(c.getName()).append(" ");
            out.println("callers: " + cs);
            DecompileResults r = dec.decompileFunction(f, 120, monitor);
            if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        }
        out.close();
    }
}