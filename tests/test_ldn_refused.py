"""An unimplemented nn::ldn import must not be answered 0.

The generic import stub (a64_unimplemented, third_party/a64-kit/rt/a64_rt.c) sets x0 = 0, and in the nn:: SDK 0 is
ResultSuccess. For a service nothing implements that reports every call as having worked while leaving the game's
out-parameters untouched, which is how nn::account's token cache once hung Ocarina (host/nn_applet.c's comment on
EnsureNetworkServiceAccountIdTokenCacheAsync). nn::ldn is local wireless and nothing implements it, so its stubs are
routed to host/nn_ldn.c's refusal instead: it logs the symbol and answers a failure.

Two halves, both against real code:
  routing   nx2/standalone.py's real gen_stubs over an authored hook list: every nn::ldn symbol goes to
            hos_ldn_refuse, everything else keeps a64_unimplemented, and the refusal is declared in the file it
            writes. The hook list is authored because the real one comes from a dump, which no check may need.
  answer    host/nn_ldn.c's real hos_ldn_refuse compiled against a fake log and a fake Ctx: the default answer is a
            non-zero Result, HOS_LDN_SOFT=1 is the old 0 and says so, and HOS_LDN_STRICT=1 stops the run.

python -I -B tests/test_ldn_refused.py [--cc gcc]
"""
import argparse
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nx2"))
import standalone   # noqa: E402

# Authored symbols in the SDK's mangling. They stand for shapes, not for an API anyone has read off a dump: nothing
# here claims these are Kirby Air Riders' ldn calls, and the real list comes from a lift (docs/kirby-air-riders.md
# section 4). What matters is the shape - free function of nn::ldn, class member of nn::ldn, another service, another
# namespace that merely starts with "ldn" - and that none of them is a symbol host/ already implements.
LDN = ["_ZN2nn3ldn10InitializeEv", "_ZN2nn3ldn8FinalizeEv", "_ZN2nn3ldn4ScanEPNS0_11NetworkInfoEPiRKNS0_10ScanFilterE"]
OTHER = ["_ZN2nn3hid22InitializeSevenSixAxisEv",   # another service, not implemented here
         "_ZN2nn3ldn7Session4OpenEv",              # a class member: lifted code, never a stub
         "_ZN2nn6ldnmgr10InitializeEv"]            # a namespace that only starts with "ldn"

FIXTURE = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define A64_NOESC
typedef int32_t LONG;
typedef struct { uint64_t x[32]; } Ctx;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL:%%d %%s\n", __LINE__, #x); exit(1); } } while (0)
#define InterlockedExchange fixture_InterlockedExchange
static LONG InterlockedExchange(volatile LONG *p, LONG v) { const LONG old = *p; *p = v; return old; }
static char last_log[512];
static int logs, fatals;
static void hos_log(const char *format, ...) {
    va_list args; va_start(args, format); vsnprintf(last_log, sizeof last_log, format, args); va_end(args);
    logs++;
}
static void hos_fatal(Ctx *c, const char *format, ...) { (void)c; (void)format; fatals++; }
%(actual)s
int main(void) {
    Ctx c = {{0}};
    c.x[30] = 0x1234;
    int seen = 0;
    const char *mode = getenv("LDN_MODE") ? getenv("LDN_MODE") : "";
    for (int i = 0; i < 3000; i++) {
        c.x[0] = 0xdead;
        hos_ldn_refuse(&c, "_ZN2nn3ldn10InitializeEv", &seen);
        if (!strcmp(mode, "soft")) CHECK(c.x[0] == 0);
        else if (!strcmp(mode, "strict")) CHECK(fatals == i + 1);
        else CHECK(c.x[0] != 0 && c.x[0] == LDN_REFUSED && (c.x[0] & 0x1ff) == LDN_MODULE);
    }
    CHECK(seen == 3000);
    /* every call is seen: the first few in full, then one in a thousand, so a caller that never stops asking shows */
    if (strcmp(mode, "strict")) {
        CHECK(logs >= 5 && logs <= 12);
        CHECK(strstr(last_log, "_ZN2nn3ldn10InitializeEv") && strstr(last_log, "not implemented"));
    }
    if (!strcmp(mode, "soft")) CHECK(strstr(last_log, "HOS_LDN_SOFT"));
    if (!*mode) CHECK(strstr(last_log, "refused"));
    printf("PASS: ldn refusal %%s\n", *mode ? mode : "default");
    return 0;
}
"""


class RoutingTests(unittest.TestCase):
    def stubs(self):
        with tempfile.TemporaryDirectory(prefix="ldn-stubs-") as tmp:
            meta = {"hooks": [{"sym": s, "c": "imp_" + s} for s in LDN + OTHER]}
            count = standalone.gen_stubs(tmp, meta)
            self.assertEqual(count, len(LDN) + len(OTHER))
            return (Path(tmp) / "a64_stubs.c").read_text(encoding="utf-8")

    def test_ldn_imports_are_refused_and_others_are_not(self):
        text = self.stubs()
        for sym in LDN:
            line = next(l for l in text.splitlines() if "imp_" + sym + "(Ctx" in l)
            self.assertIn("hos_ldn_refuse(c,", line, sym)
            self.assertNotIn("a64_unimplemented", line, sym)
        for sym in OTHER:
            line = next(l for l in text.splitlines() if "imp_" + sym + "(Ctx" in l)
            self.assertIn("a64_unimplemented(c,", line, sym)
            self.assertNotIn("hos_ldn_refuse", line, sym)

    def test_the_refusal_is_declared_in_the_generated_file(self):
        self.assertIn("void hos_ldn_refuse(Ctx *A64_NOESC c, const char *sym, int *seen);", self.stubs())

    def test_the_host_defines_it(self):
        src = (ROOT / "host" / "nn_ldn.c").read_text(encoding="utf-8")
        self.assertIn("void hos_ldn_refuse(Ctx *A64_NOESC c, const char *sym, int *seen) {", src)
        # it must not look like an HLE or an imp_ function, or nx2/standalone.py would take it for a hooked symbol
        self.assertNotIn("HLE(", src)
        self.assertNotIn("void imp_", src)

    def test_ldn_is_still_an_os_facing_namespace(self):
        # (if it were dropped from OS_NAMESPACES the imports would be lifted SDK code, not stubs at all)
        self.assertIn("ldn", standalone.OS_NAMESPACES)
        self.assertEqual(standalone.refusal_for(LDN[0]), "hos_ldn_refuse")
        self.assertIsNone(standalone.refusal_for(OTHER[0]))


class AnswerTests(unittest.TestCase):
    def test_the_answer_is_a_failure(self):
        import shutil
        cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            self.skipTest("no C compiler")
        src = (ROOT / "host" / "nn_ldn.c").read_text(encoding="utf-8")
        at = src.index("#define LDN_MODULE")
        actual = src[at:]   # (the Result constants, the mode switch and the refusal: everything below the includes)
        self.assertIn("hos_ldn_refuse", actual)
        with tempfile.TemporaryDirectory(prefix="ldn-answer-") as tmp:
            source = Path(tmp) / "t.c"
            source.write_text(FIXTURE % {"actual": actual}, encoding="utf-8")
            binary = Path(tmp) / ("t.exe" if sys.platform == "win32" else "t")
            build = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
                                   text=True, capture_output=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            for mode, env in (("default", {}), ("soft", {"HOS_LDN_SOFT": "1"}), ("strict", {"HOS_LDN_STRICT": "1"})):
                run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=60,
                                     env=dict(os.environ, LDN_MODE="" if mode == "default" else mode, **env))
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                print(run.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    args, rest = parser.parse_known_args()
    if args.cc:
        os.environ["CC"] = args.cc
    raise SystemExit(unittest.main(argv=[sys.argv[0]] + rest, verbosity=1))
