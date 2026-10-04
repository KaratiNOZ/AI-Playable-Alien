// DataRefs <hex+hex...> <out> — кто читает/пишет глобал (тип ссылки) + декомпиляция пишущих функций
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;

public class DataRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        Set<Address> done = new HashSet<>();
        for (String h : getScriptArgs()[0].split("\\+")) {
            Address a = toAddr(Long.parseLong(h, 16));
            out.println("\n##### " + a);
            for (Reference r : getReferencesTo(a)) {
                Function f = getFunctionContaining(r.getFromAddress());
                out.println("  " + r.getReferenceType() + " from " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName() + " @ " + f.getEntryPoint()));
                if (f != null && r.getReferenceType().isWrite() && done.add(f.getEntryPoint())) {
                    DecompileResults d = dec.decompileFunction(f, 90, monitor);
                    if (d != null && d.decompileCompleted()) out.println(d.getDecompiledFunction().getC());
                }
            }
        }
        out.close();
    }
}
