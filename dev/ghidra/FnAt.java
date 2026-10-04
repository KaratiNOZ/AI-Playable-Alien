// FnAt <hex+hex...> <out> — функция, содержащая адрес, и её декомпиляция
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
public class FnAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        for (String a : getScriptArgs()[0].split("[+]")) {
            Function f = getFunctionContaining(toAddr(Long.parseLong(a, 16)));
            if (f == null) { out.println("==== " + a + ": no function"); continue; }
            out.println("==== " + a + " in " + f.getName() + " @ " + f.getEntryPoint());
            DecompileResults r = dec.decompileFunction(f, 90, monitor);
            if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        }
        out.close();
    }
}
