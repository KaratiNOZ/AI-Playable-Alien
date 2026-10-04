// Dwords <from hex> <count> <out> — dword'ы подряд: значение, функция/символ по значению, метка на адресе
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class Dwords extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address p = toAddr(Long.parseLong(a[0], 16));
        int n = Integer.parseInt(a[1]);
        PrintWriter out = new PrintWriter(a[2]);
        for (int i = 0; i < n; i++) {
            Address at = p.add(i * 4);
            long v = getInt(at) & 0xffffffffL;
            String lab = "";
            Symbol s = getSymbolAt(at);
            if (s != null) lab = "  <" + s.getName(true) + ">";
            String fn = "";
            try { Function f = getFunctionAt(toAddr(v)); if (f != null) fn = f.getName(); } catch (Exception e) {}
            out.println(at + "  " + String.format("%08x", v) + "  " + fn + lab);
        }
        out.close();
    }
}
