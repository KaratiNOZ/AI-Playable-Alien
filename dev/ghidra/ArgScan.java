// ArgScan <file with "start end" lines> <out> — для каждого места вызова: PUSH-аргументы в последних 0x30 байтах
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class ArgScan extends GhidraScript {
    @Override
    protected void run() throws Exception {
        BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]));
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        String line;
        while ((line = r.readLine()) != null) {
            String[] p = line.trim().split(" ");
            if (p.length < 2) continue;
            long s = Long.parseLong(p[0], 16), e = Long.parseLong(p[1], 16);
            Function f = getFunctionContaining(toAddr(e));
            StringBuilder sb = new StringBuilder();
            Instruction ins = getInstructionAt(toAddr(s));
            if (ins == null) ins = getInstructionAfter(toAddr(s));
            while (ins != null && ins.getAddress().getOffset() <= e) {
                if (ins.getMnemonicString().equals("PUSH")) sb.append(ins.toString()).append("; ");
                ins = ins.getNext();
            }
            out.println(p[1] + "\t" + (f == null ? "?" : f.getName()) + "\t" + sb);
        }
        out.close();
    }
}
