// RefScan <hex+hex...> <out> — поиск LE-адресов в коде (push/mov/lea), создание функции и декомпиляция
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;
import java.util.*;

public class RefScan extends GhidraScript {
    DecompInterface dec; PrintWriter out; Set<Address> done = new HashSet<>();
    void dump(Function f, String tag) {
        if (f == null || !done.add(f.getEntryPoint())) return;
        out.println("---- " + tag + " " + f.getName() + " @ " + f.getEntryPoint());
        DecompileResults r = dec.decompileFunction(f, 120, monitor);
        if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
    }
    boolean uses(Instruction ins, long target) {
        for (int op = 0; op < ins.getNumOperands(); op++) {
            Scalar sc = ins.getScalar(op);
            if (sc != null && sc.getUnsignedValue() == target) return true;
            Address a = ins.getAddress(op);
            if (a != null && a.getOffset() == target) return true;
        }
        return false;
    }
    @Override
    protected void run() throws Exception {
        out = new PrintWriter(getScriptArgs()[1]);
        dec = new DecompInterface(); dec.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        for (String h : getScriptArgs()[0].split("\\+")) {
            long target = Long.parseLong(h, 16);
            out.println("\n##### refs to " + h);
            byte[] pat = new byte[4];
            for (int i = 0; i < 4; i++) pat[i] = (byte)(target >>> (8*i));
            Address a = mem.findBytes(mem.getMinAddress(), pat, null, true, monitor);
            int n = 0;
            while (a != null && n < 60) {
                n++;
                boolean hit = false;
                for (int d = 1; d <= 6 && !hit; d++) {
                    Address s = a.subtract(d);
                    if (getInstructionAt(s) == null) disassemble(s);
                    Instruction ins = getInstructionAt(s);
                    if (ins == null || !uses(ins, target)) continue;
                    Function f = getFunctionContaining(s);
                    if (f == null) { new CreateFunctionCmd(s).applyTo(currentProgram, monitor); f = getFunctionAt(s); }
                    dump(f, "ref @ " + s);
                    hit = true;
                }
                if (!hit) out.println("raw hit @ " + a + " (no instruction)");
                a = mem.findBytes(a.add(1), pat, null, true, monitor);
            }
        }
        out.close();
    }
}