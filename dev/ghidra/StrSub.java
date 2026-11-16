// StrSub <fragment+fragment...> <out> — все строки, содержащие фрагмент (полная строка от \0 до \0)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;

public class StrSub extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] frags = getScriptArgs()[0].split("\\+");
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        Memory mem = currentProgram.getMemory();
        for (String s : frags) {
            out.println("\n##### \"" + s + "\"");
            byte[] pat = s.getBytes("ASCII");
            Address a = mem.findBytes(mem.getMinAddress(), pat, null, true, monitor);
            int n = 0;
            while (a != null && n < 200) {
                Address start = a;
                while (start.compareTo(mem.getMinAddress()) > 0 && mem.getByte(start.subtract(1)) != 0) start = start.subtract(1);
                Address end = a;
                byte b;
                do { b = mem.getByte(end); end = end.add(1); } while (b != 0 && end.getOffset() - a.getOffset() < 512);
                byte[] buf = new byte[(int)(end.getOffset() - start.getOffset()) - 1];
                mem.getBytes(start, buf);
                out.println(start + "  \"" + new String(buf, "ASCII") + "\"");
                n++;
                a = mem.findBytes(a.add(1), pat, null, true, monitor);
            }
        }
        out.close();
    }
}