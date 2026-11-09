/* nn::ldn (local wireless): not implemented, and it says so.
 *
 * (kirby) ldn is the service a Switch game uses to find and join the consoles next to it over the console's own radio.
 * Nothing here implements it, and nothing here is going to: a transport that stands in for a radio, under the whole
 * session and state machine the service presents, with the game's own packet layer on top, is a project of its own.
 *
 * What this file is for is the answer the runtime gives in the meantime, because the answer it gave before was a lie.
 * nn::ldn is in nx2/standalone.py's OS_NAMESPACES, so every nn::ldn function the game imports is replaced at the nn::
 * boundary by a generated stub, and that stub called a64_unimplemented (third_party/a64-kit/rt/a64_rt.c), which prints
 * one line to stderr the first time a symbol is reached and then sets x0 = 0 and returns. In the nn:: SDK 0 is
 * ResultSuccess. So the runtime's answer to "start local wireless" was "started fine", to "open an access point" was
 * "it is open", and to a scan was "the scan succeeded" - while writing nothing into the out-parameters the game
 * passed, which keep whatever was on the stack. That is worse than a refusal in every way: it is false, it is silent
 * after the first call, and what the game does with the uninitialized memory afterwards has no traceable cause.
 *
 * The same mistake was found and fixed once already for another service: nn_applet.c's comment on
 * EnsureNetworkServiceAccountIdTokenCacheAsync records that the generic stub's 0 said the task had started and the
 * game then waited forever on an AsyncContext nothing had set up. Returning a real failure was the fix.
 *
 * So: every nn::ldn call is logged, with its symbol and the caller it came from, and answered with a failure. A game
 * that cannot start local wireless has to cope - wireless can be switched off or missing on real hardware - so it
 * falls back to single player, which is the most this engine can honestly offer. Nothing fabricates a network.
 *
 *   (default)           the call is logged and answered with a failure Result in the ldn module.
 *   HOS_LDN_STRICT=1    the first ldn call stops the run and names the symbol, as HOS_NVN_STRICT does for NVN procs
 *                       (host/nvn_native.c). For whoever wants the failure to be a wall.
 *   HOS_LDN_SOFT=1      x0 = 0, the old answer. Only for surveying how far a first boot gets; the log says in so many
 *                       words that every answer from then on is a fake.
 *
 * The honest limit of this: x0 is all the runtime can set, and it does not know the function's signature. For a
 * Result-returning function - which is nearly all of nn::ldn - a non-zero x0 is right. For one that returns a state or
 * a bool it is wrong, and wrong towards "something is set" rather than "nothing is there". Nothing can be done about
 * that here without the signatures, and they are not guessed at: the log names every ldn symbol the game reaches, so
 * the first real boot of a game that uses local wireless produces the exact list that needs real answers.
 *
 * docs/kirby-air-riders.md section 4 has the reasoning and how that list would be read off a lift.
 */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* nn::Result is module | (description << 9); 0 is success, so any other value is a failure. Module 203 is ldn's, by
 * the public error-code list (an ldn error is reported as 2203-xxxx). The description is deliberately left 0: the
 * descriptions the console returns for "no wireless" are not known here and are not invented, and the only property
 * this value needs is that it is not success. A game that compares a result against a particular nn::ldn::Result*
 * takes its default branch, which is the branch a game must have for hardware whose radio is unavailable. */
#define LDN_MODULE 203
#define LDN_REFUSED (LDN_MODULE | (0 << 9))

static int ldn_mode(void) {   /* 0 refuse, 1 stop the run, 2 the old x0 = 0 */
    static volatile LONG mode;
    LONG m = mode;
    if (!m) {
        const char *strict = getenv("HOS_LDN_STRICT"), *soft = getenv("HOS_LDN_SOFT");
        m = 1 + (strict && atoi(strict) ? 1 : soft && atoi(soft) ? 2 : 0);   /* (+1: 0 means "not read yet") */
        InterlockedExchange(&mode, m);
        if (m == 3)
            hos_log("ldn: HOS_LDN_SOFT=1: local wireless calls are answered 0 (success) although nothing implements "
                    "them. Every answer from here on is a fake and the game's local play will not work.");
    }
    return (int)m - 1;
}

/* A generated stub for an nn::ldn function (nx2/standalone.py routes them here instead of to a64_unimplemented).
   seen counts this symbol's calls, so a function the game polls every frame does not go quiet after one line. */
void hos_ldn_refuse(Ctx *A64_NOESC c, const char *sym, int *seen) {
    const int mode = ldn_mode();
    const int n = ++*seen;
    if (mode == 1)
        hos_fatal(c, "ldn: local wireless is not implemented: %s (lr %#llx). HOS_LDN_STRICT=1 asked for this stop; "
                     "without it the call is refused and the game falls back to single player.",
                  sym, (unsigned long long)c->x[30]);
    /* the first calls in full, then every 1000th: enough to show a caller that never stops asking */
    if (n <= 4 || !(n % 1000))
        hos_log("ldn: not implemented, %s: %s (call %d, lr %#llx)", mode == 2 ? "answered 0 (HOS_LDN_SOFT)" : "refused",
                sym, n, (unsigned long long)c->x[30]);
    c->x[0] = mode == 2 ? 0 : LDN_REFUSED;
}
