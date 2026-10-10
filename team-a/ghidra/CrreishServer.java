// Project Crreish, Team A: persistent, read-only, hidden "Ghidra server" — the loop-mode companion to
// CrreishDump.java. Standard usage runs analyzeHeadless.bat once per job, which pays the full JVM start
// plus 467MB SR3.rep project-open cost (observed ~13-15s) EVERY query. CrreishServer instead opens the
// project/program ONCE (same -process SaintsRowTheThird.exe, -noanalysis -readOnly as always — nothing is
// ever written back) and then sits in a loop, picking up one job at a time from a queue directory and
// running it with the exact same CrreishDump.processJob() logic, so the 2nd..Nth query in a session cost
// only the query's own work (typically ~1-3s) instead of the full reopen.
//
// This is a GhidraScript like any other: started the same way as CrreishDump.java (analyzeHeadless.bat
// -noanalysis -readOnly -scriptPath <this dir> -postScript CrreishServer.java <queueDir> [idleMinutes]
// [pollMillis]), just one that doesn't return after a single job. See tools/ghidra_server/README.md for
// the full usage contract (job-file format, result-file naming, start/stop scripts) — that is the doc to
// read before writing a new caller; this header only covers the wire protocol between this script and the
// queue directory.
//
// Protocol (all paths inside <queueDir>, one call = one file, so no job overwrites another's pending work):
//   <id>.job     plain text, one CrreishDump argument per line (same meaning as CrreishDump's own
//                <outDir> <mode> [key:value ...] <item> ... — blank lines ignored). The caller writes this
//                file LAST (after its outDir already exists) so the server never reads a half-written job.
//   <id>.done    written by the server after a job finishes successfully; the job's own output is wherever
//                its own outDir (args[0] inside the .job file) said to put it, same as plain CrreishDump.
//   <id>.error   written instead of .done if processJob threw; contains the stack trace as text.
//   STOP         drop an empty file with this exact name to ask the server to exit after its current job
//                (checked once per poll, then deleted). stop_server.ps1 does this for you.
//   server.heartbeat   rewritten after every job and every idle poll; contents = epoch millis + "idle"/
//                "busy", so a caller can tell a live server from a crashed one without touching Ghidra.
//
// Exits on its own after <idleMinutes> (default 120) with no job seen, so a forgotten server doesn't sit
// holding the project (and a chunk of RAM) open forever on the owner's gaming PC.
//
// Read-only, same limits as CrreishDump.java's standing rule: -readOnly/nothing saved, output only through
// writer() inside each job's own outDir, no network access, no process launches.
//
// @category Crreish

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

public class CrreishServer extends CrreishDump {

    @Override
    public void run() throws Exception {
        String[] sargs = getScriptArgs();
        if (sargs.length < 1) {
            printerr("usage: CrreishServer <queueDir> [idleMinutes=120] [pollMillis=300]");
            return;
        }
        File queueDir = new File(sargs[0]);
        queueDir.mkdirs();
        long idleMillis = (sargs.length > 1 ? Long.parseLong(sargs[1]) : 120L) * 60_000L;
        long pollMillis = sargs.length > 2 ? Long.parseLong(sargs[2]) : 300L;
        File stopFile = new File(queueDir, "STOP");
        File heartbeatFile = new File(queueDir, "server.heartbeat");

        println("CrreishServer: watching " + queueDir.getAbsolutePath()
                + " (idle timeout " + (idleMillis / 60000) + " min, poll " + pollMillis + " ms)");
        heartbeat(heartbeatFile, "idle", 0);
        long lastActivity = System.currentTimeMillis();
        int jobsDone = 0;

        try {
            while (true) {
                if (monitor.isCancelled()) {
                    println("CrreishServer: monitor cancelled, exiting");
                    break;
                }
                if (stopFile.exists()) {
                    stopFile.delete();
                    println("CrreishServer: STOP sentinel seen, exiting after " + jobsDone + " job(s)");
                    break;
                }
                File[] jobFiles = queueDir.listFiles((d, n) -> n.endsWith(".job"));
                if (jobFiles != null && jobFiles.length > 0) {
                    Arrays.sort(jobFiles, Comparator.comparingLong(File::lastModified));
                    for (File jf : jobFiles) {
                        if (stopFile.exists()) break; // don't start new work once asked to stop
                        runOneJobFile(jf);
                        jobsDone++;
                        lastActivity = System.currentTimeMillis();
                        heartbeat(heartbeatFile, "idle", jobsDone);
                    }
                } else {
                    if (System.currentTimeMillis() - lastActivity > idleMillis) {
                        println("CrreishServer: idle timeout reached (" + jobsDone + " job(s) served), exiting");
                        break;
                    }
                    heartbeat(heartbeatFile, "idle", jobsDone);
                    Thread.sleep(pollMillis);
                }
            }
        } finally {
            disposeDecompiler();
        }
    }

    /** Run one <id>.job file through CrreishDump's own processJob(), then leave a .done or .error marker.
     * The .job file is deleted up front (not after) so a job that throws, hangs, or is killed mid-flight
     * never gets silently re-run on the next poll — at most once, same as the old one-process-per-job
     * model where a crash just means no result file ever appears. */
    private void runOneJobFile(File jf) {
        String name = jf.getName();
        String id = name.substring(0, name.length() - ".job".length());
        File doneFile = new File(jf.getParentFile(), id + ".done");
        File errFile = new File(jf.getParentFile(), id + ".error");
        long t0 = System.currentTimeMillis();
        try {
            List<String> rawLines = Files.readAllLines(jf.toPath(), StandardCharsets.UTF_8);
            jf.delete(); // claim it before doing any work
            List<String> lines = new ArrayList<>();
            for (String l : rawLines) if (!l.isEmpty()) lines.add(l);
            println("CrreishServer: job " + id + " (" + lines.size() + " args)");
            processJob(lines.toArray(new String[0]));
            try (PrintWriter dw = new PrintWriter(new FileOutputStream(doneFile), false)) {
                dw.println("ok " + (System.currentTimeMillis() - t0) + "ms");
            }
            println("CrreishServer: job " + id + " done in " + (System.currentTimeMillis() - t0) + "ms");
        } catch (Exception ex) {
            jf.delete();
            try (PrintWriter dw = new PrintWriter(new FileOutputStream(errFile), false)) {
                ex.printStackTrace(dw);
            } catch (Exception ignore) { /* best effort */ }
            println("CrreishServer: job " + id + " FAILED: " + ex);
        }
    }

    private void heartbeat(File f, String state, int jobsDone) {
        try (PrintWriter w = new PrintWriter(new FileOutputStream(f), false)) {
            w.println(System.currentTimeMillis() + " " + state + " jobsDone=" + jobsDone);
        } catch (Exception ignore) {
            // best effort; a missing heartbeat just means the caller falls back to checking the PID file
        }
    }
}
