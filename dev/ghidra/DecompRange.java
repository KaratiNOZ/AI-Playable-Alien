// DecompRange <from hex> <to hex> <out> — декомпиляция всех функций в диапазоне адресов
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;

public class DecompRange extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address from = toAddr(Long.parseLong(a[0], 16)), to = toAddr(Long.parseLong(a[1], 16));
        PrintWriter out = new PrintWriter(a[2]);
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(from, true);
        while (it.hasNext()) {
            Function f = it.next();
            if (f.getEntryPoint().compareTo(to) > 0) break;
            out.println("---- " + f.getName() + " @ " + f.getEntryPoint() + " (callers: " + f.getCallingFunctions(monitor).size() + ")");
            DecompileResults r = dec.decompileFunction(f, 60, monitor);
            if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        }
        out.close();
    }
}
