// UseOff <global hex> <offset hex> <out> — где пишут в [*(global) + off]: после чтения глобала в регистр ищем запись [reg+off]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class UseOff extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address g = toAddr(Long.parseLong(a[0], 16));
        String off = "0x" + Long.toHexString(Long.parseLong(a[1], 16));
        PrintWriter out = new PrintWriter(a[2]);
        Listing L = currentProgram.getListing();
        for (Reference r : getReferencesTo(g)) {
            Instruction ins = L.getInstructionAt(r.getFromAddress());
            if (ins == null || !ins.getMnemonicString().equals("MOV")) continue;
            String reg = ins.getDefaultOperandRepresentation(0);
            Instruction cur = ins;
            for (int i = 0; i < 60 && cur != null; i++) {
                cur = cur.getNext();
                if (cur == null) break;
                String o0 = cur.getDefaultOperandRepresentation(0);
                if (cur.toString().contains("[" + reg + " + " + off + "]")) {
                    Function f = getFunctionContaining(cur.getAddress());
                    out.println(cur.getAddress() + "  " + cur + "   in " + (f == null ? "?" : f.getName()));
                }
                if (cur.getDefaultOperandRepresentation(0).equals(reg) && !cur.getMnemonicString().startsWith("CMP") && !cur.getMnemonicString().startsWith("TEST")) break;
            }
        }
        out.close();
    }
}
