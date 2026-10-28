#!/usr/bin/env python3
import os
import sys
import glob
import subprocess
import tempfile
import argparse

def run_test(compiler_path, sy_path, opt_level=0, timeout_sec=45, verbose=False, expected_output=None, runtime_library=None):
    base_name = os.path.splitext(sy_path)[0]
    out_file = base_name + ".out"
    in_file = base_name + ".in"

    if expected_output is None and os.path.exists(out_file):
        with open(out_file, "r", encoding="utf-8", errors="ignore") as f:
            expected_output = f.read()

    if expected_output is None:
        return False, f"Missing expected output: {out_file}"

    input_data = ""
    if os.path.exists(in_file):
        with open(in_file, "r", encoding="utf-8", errors="ignore") as f:
            input_data = f.read()

    with tempfile.TemporaryDirectory(dir="/dev/shm" if os.path.exists("/dev/shm") else "/tmp") as tmpdir:
        s_file = os.path.join(tmpdir, "out.s")
        bin_file = os.path.join(tmpdir, "out.bin")

        # Step 1: Compile .sy to .s
        cmd_compile = [compiler_path, sy_path, "-S", "-o", s_file]
        if opt_level > 0:
            cmd_compile.append(f"-O{opt_level}")

        res_compile = subprocess.run(cmd_compile, capture_output=True, text=True, timeout=60)
        if res_compile.returncode != 0:
            return False, f"Compile error: {res_compile.stderr.strip() or res_compile.stdout.strip()}"

        # Step 2: Assemble & link
        sylib_c = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "runtime", "sylib.c")
        cmd_link = [
            "aarch64-linux-gnu-gcc-11",
            "-march=armv8-a",
            "-static",
            s_file,
            runtime_library or sylib_c,
            "-o", bin_file,
            "-lm"
        ]
        res_link = subprocess.run(cmd_link, capture_output=True, text=True, timeout=30)
        if res_link.returncode != 0:
            return False, f"Link error: {res_link.stderr.strip()}"

        # Step 3: Run under QEMU
        cmd_run = ["qemu-aarch64", "-L", "/usr/aarch64-linux-gnu", bin_file]
        try:
            res_run = subprocess.run(
                cmd_run,
                input=input_data,
                capture_output=True,
                text=True,
                timeout=timeout_sec
            )
        except subprocess.TimeoutExpired:
            return False, f"Runtime timeout ({timeout_sec}s)"

        actual_stdout = res_run.stdout
        exit_code = res_run.returncode
        if exit_code < 0:
            return False, f"Runtime terminated by signal {-exit_code}: {res_run.stderr.strip()}"
        # Mask exit code to 8-bit unsigned
        exit_code_masked = exit_code & 0xFF

        actual_formatted = actual_stdout
        if actual_formatted and not actual_formatted.endswith("\n"):
            actual_formatted += "\n"
        actual_formatted += f"{exit_code_masked}\n"

        if expected_output is not None:
            # Normalize whitespace/line endings
            exp_clean = expected_output.replace("\r\n", "\n").rstrip()
            act_clean = actual_formatted.replace("\r\n", "\n").rstrip()

            if act_clean != exp_clean:
                return False, f"Output mismatch:\nExpected:\n{exp_clean[:300]}\nActual:\n{act_clean[:300]}"

        return True, "PASSED"

def main():
    parser = argparse.ArgumentParser(description="Test runner for SysY official tests")
    parser.add_argument("--compiler", default="build/compiler", help="Path to compiler")
    parser.add_argument("--dir", default="tests/official/functional", help="Directory of tests")
    parser.add_argument("-O", "--opt", default="0", help="Optimization level (0 or 1, or -O0, -O1)")
    parser.add_argument("--runtime-library", help="Official libsysy.a to link instead of the local validation runtime")
    parser.add_argument("--timeout", type=int, default=45, help="Execution timeout in seconds")
    parser.add_argument("--filter", default="", help="Filter test names")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    opt_str = str(args.opt)
    if opt_str.startswith("-O"):
        opt_str = opt_str[2:]
    opt_level = int(opt_str) if opt_str.isdigit() else 0

    sy_files = sorted(glob.glob(os.path.join(args.dir, "*.sy")) + glob.glob(os.path.join(args.dir, "*.sysy")))
    if args.filter:
        sy_files = [f for f in sy_files if args.filter in os.path.basename(f)]

    if not sy_files:
        print("Error: no test cases selected", file=sys.stderr)
        return 1

    print(f"Running {len(sy_files)} tests from {args.dir} (Opt: -O{opt_level})...")

    passed = 0
    failed = 0
    errors = []

    for idx, sy_file in enumerate(sy_files, 1):
        name = os.path.basename(sy_file)
        try:
            ok, msg = run_test(args.compiler, sy_file, opt_level=opt_level, timeout_sec=args.timeout, verbose=args.verbose, runtime_library=args.runtime_library)
            if ok:
                passed += 1
                print(f"[{idx:3d}/{len(sy_files):3d}] {name:<35} PASSED")
            else:
                failed += 1
                print(f"[{idx:3d}/{len(sy_files):3d}] {name:<35} FAILED: {msg[:100]}")
                errors.append((name, msg))
        except Exception as e:
            failed += 1
            print(f"[{idx:3d}/{len(sy_files):3d}] {name:<35} ERROR: {e}")
            errors.append((name, str(e)))

    print("\n" + "="*70)
    print(f"RESULTS: {passed}/{len(sy_files)} passed ({passed*100.0/len(sy_files) if sy_files else 0:.1f}%), {failed} failed.")
    print("="*70)

    if errors:
        print("\nSummary of Failures:")
        for name, err in errors[:25]:
            print(f"  - {name}: {err.splitlines()[0] if err else ''}")
        if len(errors) > 25:
            print(f"  ... and {len(errors)-25} more failures.")

    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
