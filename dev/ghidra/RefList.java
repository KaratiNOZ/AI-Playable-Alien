// RefList <hex+hex...> <out> — все ссылки на глобал (тип, откуда, функция), без декомпиляции
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class RefList extends GhidraScript {
    @Override
    protected void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[1]);
        for (String h : getScriptArgs()[0].split("\\+")) {
            Address a = toAddr(Long.parseLong(h, 16));
            out.println("\n##### " + a);
            for (Reference r : getReferencesTo(a)) {
                Function f = getFunctionContaining(r.getFromAddress());
                out.println("  " + r.getReferenceType() + " " + r.getFromAddress() + " " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
            }
        }
        out.close();
    }
}
