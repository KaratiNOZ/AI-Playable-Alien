// FindVt <substr+substr...> <out> — все vftable-символы, в полном имени которых есть подстрока (без учёта регистра)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class FindVt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] subs = getScriptArgs()[0].toLowerCase().split("\\+");
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(true)) {
            if (!s.getName().equals("vftable")) continue;
            String full = s.getName(true), l = full.toLowerCase();
            for (String x : subs) if (l.contains(x)) { out.println(s.getAddress() + "  " + full); break; }
        }
        out.close();
    }
}
