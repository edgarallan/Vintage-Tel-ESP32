"""Unisce i dati gcov di tutti gli eseguibili di test e riporta la copertura di core/."""

import collections
import pathlib
import re
import subprocess
import sys

TARGET_PCT = 80.0


def main(build_dir: str) -> int:
    root = pathlib.Path(build_dir).resolve()
    work = root / "_gcov"
    work.mkdir(exist_ok=True)

    covered: dict[str, set[int]] = collections.defaultdict(set)
    missed: dict[str, set[int]] = collections.defaultdict(set)

    for gcno in root.rglob("*.gcno"):
        subprocess.run(
            ["xcrun", "llvm-cov", "gcov", "-o", str(gcno.parent), str(gcno)],
            cwd=work, capture_output=True,
        )
        for report in work.glob("*.c.gcov"):
            src = report.name[:-5]
            for line in report.read_text(errors="ignore").splitlines():
                m = re.match(r"^\s*([#\-0-9]+)\*?:\s*(\d+):", line)
                if not m:
                    continue
                count, lineno = m.group(1), int(m.group(2))
                if lineno == 0 or count == "-":
                    continue
                (missed if count == "#####" else covered)[src].add(lineno)
            report.unlink()

    total_c = total_m = 0
    print(f"{'modulo':<18} {'copertura':>10}   righe")
    print("-" * 48)
    for src in sorted(set(covered) | set(missed)):
        if not src.endswith(".c") or any(x in src for x in ("test", "unity", "fake")):
            continue
        hit = covered[src]
        gap = missed[src] - hit
        pct = 100 * len(hit) / (len(hit) + len(gap)) if (hit or gap) else 0.0
        total_c += len(hit)
        total_m += len(gap)
        print(f"{src[:-2]:<18} {pct:>9.1f}%   {len(hit)}/{len(hit) + len(gap)}")
        if gap:
            print(f"{'':18} scoperte: {sorted(gap)}")

    overall = 100 * total_c / (total_c + total_m) if (total_c + total_m) else 0.0
    print("-" * 48)
    print(f"{'TOTALE core/':<18} {overall:>9.1f}%   {total_c}/{total_c + total_m}")

    if overall < TARGET_PCT:
        print(f"\nSotto la soglia del {TARGET_PCT:.0f}%.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "tests/cov"))
