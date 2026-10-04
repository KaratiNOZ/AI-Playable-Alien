// Disasm <from hex> <to hex> <out> — листинг инструкций в диапазоне
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;

public class Disasm extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address from = toAddr(Long.parseLong(a[0], 16)), to = toAddr(Long.parseLong(a[1], 16));
        PrintWriter out = new PrintWriter(a[2]);
        Instruction ins = currentProgram.getListing().getInstructionAt(from);
        if (ins == null) ins = currentProgram.getListing().getInstructionAfter(from);
        while (ins != null && ins.getAddress().compareTo(to) <= 0) {
            out.println(ins.getAddress() + "  " + ins);
            ins = ins.getNext();
        }
        out.close();
    }
}
