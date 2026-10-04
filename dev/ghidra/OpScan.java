// OpScan <substr+substr...> <out> — все инструкции, в операндах которых есть подстрока (напр. "+ 0x78c]")
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;

public class OpScan extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        String[] subs = a[0].split("\\+\\+");
        PrintWriter out = new PrintWriter(a[1]);
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            String s = ins.toString();
            for (String sub : subs) {
                if (s.contains(sub)) {
                    Function f = getFunctionContaining(ins.getAddress());
                    out.println(sub + "\t" + ins.getAddress() + "\t" + s + "\t" + (f == null ? "?" : f.getName()));
                }
            }
        }
        out.close();
    }
}
