// FindPtr <hex+hex...> <out> — где в памяти лежит dword с таким значением (vtable, таблицы); + 16 соседних dword'ов
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;

public class FindPtr extends GhidraScript {
    @Override
    protected void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        Memory mem = currentProgram.getMemory();
        for (String h : getScriptArgs()[0].split("\\+")) {
            long v = Long.parseLong(h, 16);
            byte[] pat = { (byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24) };
            out.println("##### " + h);
            Address a = mem.getMinAddress();
            int n = 0;
            while (n < 40) {
                a = mem.findBytes(a, pat, null, true, monitor);
                if (a == null) break;
                n++;
                out.print(a + ":");
                for (int k = -8; k < 12; k++) {
                    try { out.print(String.format(" %08x", mem.getInt(a.add(k * 4)) & 0xffffffffL)); } catch (Exception e) { out.print(" ?"); }
                    if (k == -1) out.print(" |");
                }
                out.println();
                a = a.add(1);
            }
        }
        out.close();
    }
}
