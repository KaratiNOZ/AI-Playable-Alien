// DecompNew <hex+hex...> <out> — создать функцию по адресу (если её нет) и декомпилировать
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;

public class DecompNew extends GhidraScript {
    @Override
    protected void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        for (String h : getScriptArgs()[0].split("\\+")) {
            Address a = toAddr(Long.parseLong(h, 16));
            Function f = getFunctionAt(a);
            if (f == null) {
                if (getInstructionAt(a) == null) disassemble(a);
                new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
                f = getFunctionAt(a);
            }
            if (f == null) { out.println("cannot create function at " + a); continue; }
            out.println("==== " + f.getName() + " @ " + f.getEntryPoint());
            DecompileResults r = dec.decompileFunction(f, 180, monitor);
            if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
            else out.println("decompile failed");
        }
        out.close();
    }
}
