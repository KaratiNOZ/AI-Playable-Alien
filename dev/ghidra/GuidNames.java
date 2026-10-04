// GuidNames <from hex> <to hex> <getFrom hex> <getTo hex> <out> — статические инициализаторы ShortGuid (push "имя"; mov ecx, глобал;
// call 0x8bf7f0) -> карта глобал=имя; затем для геттеров параметров в [getFrom, getTo) — какой глобал (имя) они читают
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;
import java.util.*;

public class GuidNames extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Listing L = currentProgram.getListing();
        Map<Long, String> names = new TreeMap<>();
        Instruction ins = L.getInstructionAt(toAddr(Long.parseLong(a[0], 16)));
        Address end = toAddr(Long.parseLong(a[1], 16));
        long lastPush = -1;
        while (ins != null && ins.getAddress().compareTo(end) < 0) {
            String m = ins.getMnemonicString();
            if (m.equals("PUSH") && ins.getScalar(0) != null) lastPush = ins.getScalar(0).getUnsignedValue();
            else if (m.equals("MOV") && ins.getDefaultOperandRepresentation(0).equals("ECX") && ins.getScalar(1) != null && lastPush > 0) {
                long g = ins.getScalar(1).getUnsignedValue();
                byte[] b = new byte[64];
                try { currentProgram.getMemory().getBytes(toAddr(lastPush), b); } catch (Exception e) { }
                int n = 0; while (n < 64 && b[n] != 0) n++;
                names.put(g, new String(b, 0, n, "ASCII"));
                lastPush = -1;
            }
            ins = ins.getNext();
        }
        PrintWriter out = new PrintWriter(a[4]);
        // геттеры
        Instruction g = L.getInstructionAt(toAddr(Long.parseLong(a[2], 16)));
        Address gend = toAddr(Long.parseLong(a[3], 16));
        while (g != null && g.getAddress().compareTo(gend) < 0) {
            for (int op = 0; op < g.getNumOperands(); op++) {
                Scalar s = g.getScalar(op);
                if (s != null && names.containsKey(s.getUnsignedValue())) {
                    Function f = getFunctionContaining(g.getAddress());
                    out.println((f == null ? "?" : f.getEntryPoint().toString()) + " " + names.get(s.getUnsignedValue()) + " (" + Long.toHexString(s.getUnsignedValue()) + ")");
                }
            }
            g = g.getNext();
        }
        out.println("---- all");
        for (Map.Entry<Long, String> e : names.entrySet()) out.println(Long.toHexString(e.getKey()) + " " + e.getValue());
        out.close();
    }
}
