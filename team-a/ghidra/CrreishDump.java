// Project Crreish, Team A: general-purpose headless dump script for the PC bridge.
//
// Runs under analyzeHeadless with -readOnly -noanalysis (see bridge/pc_agent.py). Writes plain-text
// reports into the output directory given as the first argument. The output is raw evidence for Team A
// to read and describe in its own words; it is never committed to the project repository.
//
// Arguments:  <outDir> <mode> [key=value ...] <item> [<item> ...]
//
// Modes:
//   lua   <name ...>   find each Lua-registered engine function by its registered name string: locate the
//                      string, every instruction that uses its address as an immediate, the code pointers
//                      next to that use (the {namePtr, funcPtr} registration pair), then dump each candidate.
//   func  <addr ...>   dump the function containing each address (listing, decompile, callees, globals).
//   xref  <addr ...>   every instruction anywhere that references each address (reference manager plus a
//                      raw little-endian immediate/displacement scan), with its containing function.
//   str   <text ...>   find each exact NUL-terminated ASCII string and report its uses, like `lua`
//                      without dumping the candidates.
//   ptrs  <addr ...>   read count:N consecutive dwords of a static pointer table at each address and print,
//                      per index, the value and the string it points to (if any). Read-only, like the rest.
//   range <0xS-0xE ...> disassemble [S, E] in this session (not saved) and list every instruction with its
//                      references; for code Ghidra never defined.
//
// Options (write them as key:value in bridge jobs; key=value is split apart by the Windows batch launcher):
//   depth=N      callee recursion depth for dumps (default 1; 0 = only the named functions)
//   maxfuncs=N   cap on functions dumped per item (default 40)
//   maxinsn=N    functions longer than this are listed but not dumped when reached as callees (default 600)
//   xrefs=N      per-global cap on reference sites listed inside a dump (default 25)
//   window=N     instructions scanned after a name-string use for code pointers (default 6)
//   nodecomp=1   skip the decompiler (listing only)
//   count=N      ptrs mode: number of dwords to read (default 16)
//
// @category Crreish

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

public class CrreishDump extends GhidraScript {

    private int depth = 1, maxFuncs = 40, maxInsn = 600, xrefCap = 25, window = 6, count = 16;
    private boolean decompile = true;
    private DecompInterface decomp;
    private Listing listing;
    private Memory mem;
    private File outDir;
    private PrintWriter index;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            printerr("usage: CrreishDump <outDir> <lua|func|xref|str> [key=value ...] <item> ...");
            return;
        }
        outDir = new File(args[0]);
        outDir.mkdirs();
        String mode = args[1];
        List<String> items = new ArrayList<>();
        for (int i = 2; i < args.length; i++) {
            String a = args[i];
            // ':' is accepted as well as '=': the PC agent starts Ghidra through analyzeHeadless.bat,
            // and cmd.exe splits arguments at '=', so 'depth=2' arrives as two items there.
            int eq = a.indexOf('=');
            if (eq < 0) eq = a.indexOf(':');
            if (eq > 0 && a.substring(0, eq).matches("[a-z]+")) {
                String k = a.substring(0, eq), v = a.substring(eq + 1);
                switch (k) {
                    case "depth": depth = Integer.parseInt(v); break;
                    case "maxfuncs": maxFuncs = Integer.parseInt(v); break;
                    case "maxinsn": maxInsn = Integer.parseInt(v); break;
                    case "xrefs": xrefCap = Integer.parseInt(v); break;
                    case "window": window = Integer.parseInt(v); break;
                    case "nodecomp": decompile = !"1".equals(v); break;
                    case "count": count = Integer.parseInt(v); break;
                    default: items.add(a); // not an option key: keep it as a search item (e.g. a name containing ':')
                }
            } else {
                items.add(a);
            }
        }
        listing = currentProgram.getListing();
        mem = currentProgram.getMemory();
        if (decompile) {
            decomp = new DecompInterface();
            decomp.setOptions(new DecompileOptions());
            decomp.openProgram(currentProgram);
        }
        index = writer("index.txt");
        index.println("program " + currentProgram.getName() + " image base " + currentProgram.getImageBase());
        index.println("mode " + mode + " depth=" + depth + " maxfuncs=" + maxFuncs + " maxinsn=" + maxInsn);
        for (String item : items) {
            if (monitor.isCancelled()) throw new RuntimeException("cancelled");
            index.println();
            index.println("== " + item);
            try {
                switch (mode) {
                    case "lua": doLua(item, true); break;
                    case "str": doLua(item, false); break;
                    case "func": doFunc(item); break;
                    case "xref": doXref(item); break;
                    case "ptrs": doPtrs(item); break;
                    case "range": doRange(item); break;
                    default: index.println("unknown mode " + mode); return;
                }
            } catch (Exception e) {
                index.println("ERROR " + e);
            }
            index.flush();
        }
        index.close();
        if (decomp != null) decomp.dispose();
    }

    // ---------------------------------------------------------------- lua / str

    private void doLua(String name, boolean dump) throws Exception {
        List<Address> strs = findCString(name);
        index.println("string occurrences: " + strs.size());
        if (strs.isEmpty()) {
            index.println("NOT FOUND as an exact NUL-terminated ASCII string");
            return;
        }
        Set<Address> candidates = new LinkedHashSet<>();
        for (Address s : strs) {
            index.println("  string at " + s + " (" + blockName(s) + ")");
            Set<Address> uses = usesOf(s);
            index.println("  uses: " + uses.size());
            for (Address u : uses) {
                Instruction ins = listing.getInstructionContaining(u);
                Function f = ins == null ? null : getFunctionContaining(ins.getAddress());
                index.println("    use " + u + " in " + fname(f) + ": " + (ins == null ? "(no instruction)" : ins.toString()));
                if (ins == null) continue;
                // Code pointers in the instructions around the use: the paired function pointer of a
                // {namePtr, funcPtr} registration entry, or a pushed callback.
                // The nearest pointer after the use is taken as the pair's function pointer (the next pair's
                // pointer sits further on); if there is none after it, the nearest one before it.
                Instruction p = ins;
                int back = 0;
                for (; back < 2 && p.getPrevious() != null; back++) p = p.getPrevious();
                Address after = null, before = null;
                for (int k = 0; k < window + back && p != null; k++, p = p.getNext()) {
                    int rel = k - back;
                    for (Address t : immediateTargets(p)) {
                        if (isExec(t) && !t.equals(s)) {
                            index.println("      code pointer " + t + " at insn offset " + (rel >= 0 ? "+" : "") + rel
                                    + " (" + p.getAddress() + ": " + p + ")" + (getFunctionAt(t) != null ? " [function entry]" : ""));
                            if (rel > 0 && after == null) after = t;
                            if (rel < 0) before = t;
                        }
                    }
                }
                if (after != null) candidates.add(after);
                else if (before != null) candidates.add(before);
            }
        }
        if (!dump) return;
        String tag = sanitize(name);
        int n = 0;
        for (Address c : candidates) {
            dumpTree(c, tag + "_" + (n++));
        }
        index.println("candidates dumped: " + candidates.size());
    }

    private List<Address> findCString(String text) throws Exception {
        byte[] body = text.getBytes(StandardCharsets.US_ASCII);
        byte[] pat = new byte[body.length + 2];
        pat[0] = 0;
        System.arraycopy(body, 0, pat, 1, body.length);
        pat[pat.length - 1] = 0;
        List<Address> out = new ArrayList<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized()) continue;
            Address a = b.getStart();
            // a string at the very start of a block has no leading NUL; check it separately
            if (matches(b.getStart(), body, true)) out.add(b.getStart());
            while (a != null && a.compareTo(b.getEnd()) <= 0) {
                Address hit = mem.findBytes(a, b.getEnd(), pat, null, true, monitor);
                if (hit == null) break;
                out.add(hit.add(1));
                a = hit.add(1);
            }
        }
        return out;
    }

    private boolean matches(Address a, byte[] body, boolean nulAfter) {
        try {
            byte[] buf = new byte[body.length + 1];
            if (mem.getBytes(a, buf) != buf.length) return false;
            for (int i = 0; i < body.length; i++) if (buf[i] != body[i]) return false;
            return !nulAfter || buf[body.length] == 0;
        } catch (MemoryAccessException e) {
            return false;
        }
    }

    // ---------------------------------------------------------------- func

    private void doFunc(String item) throws Exception {
        Address a = toAddr(item);
        dumpTree(a, "func_" + sanitize(item));
    }

    private Function functionFor(Address a) throws Exception {
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        if (f == null && listing.getInstructionAt(a) != null) {
            // not a defined function in the project: create it in this read-only session only
            f = createFunction(a, null);
        }
        return f;
    }

    private void dumpTree(Address root, String tag) throws Exception {
        Function rf = functionFor(root);
        if (rf == null) {
            index.println("  " + root + ": no function or instruction here");
            return;
        }
        PrintWriter w = writer(tag + ".txt");
        index.println("  dump " + tag + ".txt root " + rf.getEntryPoint() + " (" + rf.getName() + ")");
        Deque<Object[]> q = new ArrayDeque<>();
        Set<Address> seen = new LinkedHashSet<>();
        q.add(new Object[]{rf, 0});
        int dumped = 0;
        Map<Address, Integer> globals = new LinkedHashMap<>();
        while (!q.isEmpty() && dumped < maxFuncs) {
            Object[] e = q.poll();
            Function f = (Function) e[0];
            int d = (Integer) e[1];
            if (!seen.add(f.getEntryPoint())) continue;
            long n = countInsns(f);
            if (d > 0 && n > maxInsn) {
                w.println("### " + f.getEntryPoint() + " " + f.getName() + " — " + n + " instructions, over maxinsn, not dumped");
                w.println();
                continue;
            }
            dumped++;
            List<Function> callees = dumpOne(w, f, d, globals);
            if (d < depth) for (Function c : callees) if (!seen.contains(c.getEntryPoint())) q.add(new Object[]{c, d + 1});
        }
        if (!q.isEmpty()) w.println("(stopped at maxfuncs=" + maxFuncs + "; " + q.size() + " queued callees not dumped)");
        w.println();
        w.println("######## globals referenced by the dumped functions");
        for (Map.Entry<Address, Integer> g : globals.entrySet()) {
            describeGlobal(w, g.getKey(), xrefCap);
        }
        w.close();
    }

    private long countInsns(Function f) {
        long n = 0;
        InstructionIterator it = listing.getInstructions(f.getBody(), true);
        while (it.hasNext()) { it.next(); n++; }
        return n;
    }

    private List<Function> dumpOne(PrintWriter w, Function f, int d, Map<Address, Integer> globals) throws Exception {
        AddressSetView body = f.getBody();
        w.println("### " + f.getEntryPoint() + " " + f.getName() + "  depth " + d + "  body " + body.getMinAddress() + "-" + body.getMaxAddress()
                + "  convention " + f.getCallingConventionName() + "  stackPurge " + f.getStackPurgeSize());
        List<String> callers = new ArrayList<>();
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
        while (ri.hasNext()) {
            Reference r = ri.next();
            Function cf = getFunctionContaining(r.getFromAddress());
            callers.add(r.getFromAddress() + "(" + fname(cf) + "," + r.getReferenceType() + ")");
            if (callers.size() >= 30) { callers.add("..."); break; }
        }
        w.println("callers/refs: " + String.join(" ", callers));
        w.println("-- listing");
        Set<Function> callees = new LinkedHashSet<>();
        InstructionIterator it = listing.getInstructions(body, true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            StringBuilder note = new StringBuilder();
            for (Reference r : ins.getReferencesFrom()) {
                Address t = r.getToAddress();
                if (!t.isMemoryAddress()) continue;
                if (r.getReferenceType().isCall() || r.getReferenceType().isJump()) {
                    Function cf = getFunctionAt(t);
                    if (cf != null && !body.contains(t)) {
                        callees.add(cf);
                        note.append(" -> ").append(cf.getName());
                    }
                } else {
                    note.append(" [").append(r.getReferenceType()).append(' ').append(t).append(describeData(t)).append(']');
                    if (!isExec(t)) globals.merge(t, 1, Integer::sum);
                }
            }
            for (Address t : immediateTargets(ins)) {
                if (!isExec(t) && mem.contains(t) && !globals.containsKey(t)) {
                    globals.merge(t, 1, Integer::sum);
                    note.append(" [imm ").append(t).append(describeData(t)).append(']');
                } else if (isExec(t) && getFunctionAt(t) != null && !body.contains(t)) {
                    note.append(" [code ptr ").append(getFunctionAt(t).getName()).append(']');
                }
            }
            w.println("  " + ins.getAddress() + "  " + ins + note);
        }
        if (decomp != null) {
            DecompileResults res = decomp.decompileFunction(f, 120, monitor);
            w.println("-- decompile");
            if (res != null && res.decompileCompleted()) w.println(res.getDecompiledFunction().getC());
            else w.println("(decompile failed: " + (res == null ? "null" : res.getErrorMessage()) + ")");
        }
        List<String> cn = new ArrayList<>();
        for (Function c : callees) cn.add(c.getEntryPoint() + "(" + c.getName() + ")");
        w.println("-- callees: " + String.join(" ", cn));
        w.println();
        return new ArrayList<>(callees);
    }

    // ---------------------------------------------------------------- xref

    private void doXref(String item) throws Exception {
        Address a = toAddr(item);
        PrintWriter w = writer("xref_" + sanitize(item) + ".txt");
        describeGlobal(w, a, Integer.MAX_VALUE);
        w.close();
        index.println("  xref_" + sanitize(item) + ".txt");
    }

    // range: item "0xSTART-0xEND". Disassembles from START in this read-only session (nothing is saved, as
    // with createFunction above) and lists every instruction in [START, END] with its references, so code
    // Ghidra never defined (no function, no instructions) can be read.
    private void doRange(String item) throws Exception {
        String[] se = item.split("-", 2);
        Address start = toAddr(se[0]), end = toAddr(se[1]);
        long span = end.subtract(start);
        if (span < 0 || span > 0x10000) {
            index.println("  range refused: " + item + " spans " + span + " bytes (limit 0x10000; END must not precede START)");
            return;
        }
        PrintWriter w = writer("range_" + sanitize(item) + ".txt");
        w.println("@ " + start + " - " + end + " block " + blockName(start));
        Address a = start;
        while (a != null && a.compareTo(end) <= 0) {
            if (monitor.isCancelled()) throw new RuntimeException("cancelled");
            Instruction ins = listing.getInstructionAt(a);
            if (ins == null) { disassemble(a); ins = listing.getInstructionAt(a); }
            if (ins == null) {
                byte b = mem.getByte(a);
                w.println(String.format("  %s  db 0x%02x", a, b & 0xff));
                a = a.next();
                continue;
            }
            StringBuilder sb = new StringBuilder("  " + ins.getAddress() + "  " + ins);
            for (Reference r : ins.getReferencesFrom()) {
                Function tf = getFunctionContaining(r.getToAddress());
                sb.append("  -> ").append(r.getToAddress()).append(tf == null ? "" : " [" + fname(tf) + "]").append(describeData(r.getToAddress()));
            }
            w.println(sb);
            a = ins.getMaxAddress().next();
        }
        w.close();
        index.println("  range_" + sanitize(item) + ".txt");
    }

    // ptrs: read `count` consecutive little-endian dwords starting at the item address (a static pointer
    // table) and print, per index, the dword and, when it points into initialized memory, the string there.
    private void doPtrs(String item) throws Exception {
        Address a = toAddr(item);
        PrintWriter w = writer("ptrs_" + sanitize(item) + ".txt");
        w.println("@ " + a + " block " + blockName(a) + (fileBacked(a) ? " file-backed" : " NOT file-backed (zero-fill/runtime)") + ", " + count + " dwords");
        for (int i = 0; i < count; i++) {
            Address e = a.add(4L * i);
            long v = mem.getInt(e) & 0xffffffffL;
            String txt = "";
            try {
                Address t = toAddr(v);
                MemoryBlock b = mem.getBlock(t);
                if (b != null && b.isInitialized()) txt = describeData(t);
            } catch (Exception ex) { /* not an address */ }
            w.println(String.format("%4d  %s  0x%08x%s", i, e, v, txt));
        }
        w.close();
        index.println("  ptrs_" + sanitize(item) + ".txt");
    }

    private void describeGlobal(PrintWriter w, Address a, int cap) throws Exception {
        MemoryBlock b = mem.getBlock(a);
        StringBuilder hdr = new StringBuilder("@ " + a + " block " + (b == null ? "none" : b.getName()));
        if (b != null) {
            hdr.append(b.isInitialized() && fileBacked(a) ? " file-backed" : " NOT file-backed (zero-fill/runtime)");
            try {
                if (b.isInitialized()) hdr.append(String.format(" static dword 0x%08x", mem.getInt(a)));
            } catch (MemoryAccessException e) { /* ignore */ }
        }
        hdr.append(describeData(a));
        w.println(hdr);
        Set<Address> uses = usesOf(a);
        // also uses of nearby addresses through the reference manager are not included; exact address only
        int n = 0;
        Map<String, Integer> perFunc = new HashMap<>();
        for (Address u : uses) {
            Instruction ins = listing.getInstructionContaining(u);
            Function f = ins == null ? null : getFunctionContaining(ins.getAddress());
            perFunc.merge(fname(f) + "@" + (f == null ? "-" : f.getEntryPoint().toString()), 1, Integer::sum);
            if (n++ < cap) w.println("    " + u + " in " + fname(f) + (f == null ? "" : "@" + f.getEntryPoint()) + ": " + (ins == null ? "(data)" : ins.toString()) + accessKind(ins, a));
        }
        w.println("    total uses " + uses.size() + " in " + perFunc.size() + " functions" + (uses.size() > cap ? " (listing capped)" : ""));
        w.println();
    }

    private String accessKind(Instruction ins, Address a) {
        if (ins == null) return "";
        for (Reference r : ins.getReferencesFrom()) {
            if (r.getToAddress().equals(a)) return "  {" + r.getReferenceType() + "}";
        }
        return "  {imm/disp}";
    }

    /** Instruction addresses (or data addresses) that reference {@code a}: reference manager plus raw LE scan. */
    private Set<Address> usesOf(Address a) throws Exception {
        Set<Address> out = new TreeSet<>();
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a);
        while (ri.hasNext()) out.add(ri.next().getFromAddress());
        long v = a.getOffset();
        byte[] pat = new byte[]{(byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24)};
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized()) continue;
            Address s = b.getStart();
            while (s != null && s.compareTo(b.getEnd()) <= 0) {
                if (monitor.isCancelled()) throw new RuntimeException("cancelled");
                Address hit = mem.findBytes(s, b.getEnd(), pat, null, true, monitor);
                if (hit == null) break;
                Instruction ins = listing.getInstructionContaining(hit);
                out.add(ins != null ? ins.getAddress() : hit);
                s = hit.add(1);
            }
        }
        return out;
    }

    // ---------------------------------------------------------------- helpers

    private List<Address> immediateTargets(Instruction ins) {
        List<Address> out = new ArrayList<>();
        for (int i = 0; i < ins.getNumOperands(); i++) {
            for (Object o : ins.getOpObjects(i)) {
                long v = -1;
                if (o instanceof Scalar) v = ((Scalar) o).getUnsignedValue();
                else if (o instanceof Address) v = ((Address) o).getOffset();
                if (v <= 0) continue;
                try {
                    Address t = toAddr(v);
                    if (mem.contains(t)) out.add(t);
                } catch (Exception e) { /* not an address */ }
            }
        }
        return out;
    }

    private boolean isExec(Address a) {
        MemoryBlock b = mem.getBlock(a);
        return b != null && b.isExecute();
    }

    private boolean fileBacked(Address a) {
        try {
            return mem.getAddressSourceInfo(a) != null && mem.getAddressSourceInfo(a).getFileOffset() >= 0;
        } catch (Throwable t) {
            return true; // older Ghidra without source info: assume initialized means file-backed
        }
    }

    private String describeData(Address a) {
        Data d = listing.getDataContaining(a);
        if (d != null && d.hasStringValue()) {
            String s = StringDataInstance.getStringDataInstance(d).getStringValue();
            if (s != null) return " \"" + clip(s) + "\"";
        }
        // undefined bytes: try a short ASCII read
        try {
            byte[] buf = new byte[64];
            int n = mem.getBytes(a, buf);
            int len = 0;
            while (len < n && buf[len] >= 0x20 && buf[len] < 0x7f) len++;
            if (len >= 4 && len < n && buf[len] == 0) return " \"" + clip(new String(buf, 0, len, StandardCharsets.US_ASCII)) + "\"";
        } catch (Exception e) { /* ignore */ }
        return "";
    }

    private String clip(String s) {
        s = s.replace("\n", "\\n").replace("\r", "\\r");
        return s.length() > 80 ? s.substring(0, 80) + "..." : s;
    }

    private String blockName(Address a) {
        MemoryBlock b = mem.getBlock(a);
        return b == null ? "?" : b.getName();
    }

    private String fname(Function f) {
        return f == null ? "(no function)" : f.getName();
    }

    private String sanitize(String s) {
        return s.replaceAll("[^A-Za-z0-9_.-]", "_");
    }

    private PrintWriter writer(String name) throws Exception {
        return new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(outDir, name)), StandardCharsets.UTF_8));
    }
}
