ifeq ($(origin CXX), default)
CXX = $(shell command -v clang++-18 2>/dev/null || command -v clang++ 2>/dev/null)
endif
.PHONY: all check performance package clean
all:
	CXX="$(CXX)" sh scripts/build_submission.sh
check: all
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/run_tests.py --compiler ./compiler
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_audit_regressions.py --compiler ./compiler
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_contest.py --compiler ./compiler
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_middle_end.py --compiler ./compiler
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_compliance.py --compiler ./compiler
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_arm64_abi.py --compiler ./compiler
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_arm64_workloads.py --compiler ./compiler
	@for suite in functional h_functional; do for level in 0 1; do PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_official.py --compiler ./compiler --dir tests/official/$$suite -O$$level || exit 1; done; done
performance: all
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_arm64_performance.py --compiler ./compiler --report build/performance.json
package:
	python3 scripts/package_submission.py
clean:
	$(RM) -- compiler
