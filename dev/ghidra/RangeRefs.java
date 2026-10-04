// RangeRefs <from hex> <to hex> <out> — все ссылки на адреса в диапазоне
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class RangeRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long from = Long.parseLong(a[0], 16), to = Long.parseLong(a[1], 16);
        PrintWriter out = new PrintWriter(a[2]);
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (long x = from; x <= to; x++) {
            Address ad = toAddr(x);
            for (Reference r : rm.getReferencesTo(ad)) {
                Function f = getFunctionContaining(r.getFromAddress());
                Instruction ins = currentProgram.getListing().getInstructionAt(r.getFromAddress());
                out.println(ad + " " + r.getReferenceType() + " " + r.getFromAddress() + " " + (f == null ? "?" : f.getName()) + "  " + (ins == null ? "" : ins.toString()));
            }
        }
        out.close();
    }
}
