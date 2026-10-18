.PHONY: all upstreams build qemu test host clean
all: build
upstreams:
	python3 scripts/fetch_upstreams.py
build:
	bash scripts/build.sh
qemu: build
	python3 scripts/run_qemu.py
	python3 scripts/run_qemu_rock.py
	python3 scripts/run_qemu_v5.py
	python3 scripts/run_qemu_privileged.py
	python3 scripts/run_qemu_cp15.py
	python3 scripts/run_qemu_operand2.py
	python3 scripts/run_qemu_compare2.py
	python3 scripts/run_qemu_thumb_branch.py
	python3 scripts/run_qemu_immediate_alu.py
test: build
	.venv/bin/python scripts/run_desmume.py
host:
	python3 -m compileall -q scripts
	python3 scripts/test_register_width_fix.py
	python3 scripts/test_source_contract.py
	python3 scripts/test_thumb_branch_generator.py
	python3 scripts/test_immediate_alu_generator.py
	python3 scripts/test_emulator_diff.py
	python3 scripts/test_trace_formats.py
	python3 scripts/test_upstreams.py
	python3 scripts/test_release_readiness.py
clean:
	rm -rf work artifacts

.PHONY: sequential recovery
sequential: build
	.venv/bin/python scripts/run_desmume.py --sequential --output reports/sequential.json --screenshot reports/score-screen.png
recovery: build
	.venv/bin/python scripts/test_recovery.py

.PHONY: debug diagnostics
debug:
	python3 scripts/export_debug.py reports/resilient.json
diagnostics:
	.venv/bin/python scripts/test_diagnostics.py
	.venv/bin/python scripts/test_arm7_specific.py
	.venv/bin/python scripts/test_privileged.py

.PHONY: qemu-exceptions milestone8
qemu-exceptions:
	python3 scripts/run_qemu_exceptions.py
milestone8:
	.venv/bin/python scripts/test_milestone8.py

.PHONY: operand2
operand2:
	.venv/bin/python scripts/test_operand2.py

.PHONY: compare2
compare2:
	.venv/bin/python scripts/test_compare2.py

.PHONY: thumb-edges
thumb-edges:
	python3 scripts/run_qemu_thumb.py
	.venv/bin/python scripts/test_thumb_edges.py

.PHONY: cpu-groups
cpu-groups:
	.venv/bin/python scripts/test_cpu_groups.py

.PHONY: thumb-memory trace-provenance
thumb-memory:
	python3 scripts/run_qemu_thumb_memory.py
	.venv/bin/python scripts/test_thumb_memory.py
trace-provenance:
	.venv/bin/python scripts/test_trace_provenance.py

.PHONY: thumb-sp
thumb-sp:
	python3 scripts/run_qemu_thumb_sp.py
	.venv/bin/python scripts/test_thumb_sp.py

.PHONY: thumb-pc
thumb-pc:
	python3 scripts/run_qemu_thumb_pc.py
	.venv/bin/python scripts/test_thumb_pc.py

.PHONY: thumb-branch immediate-alu
thumb-branch: build
	python3 scripts/run_qemu_thumb_branch.py
	.venv/bin/python scripts/test_thumb_branch.py

immediate-alu: build
	python3 scripts/run_qemu_immediate_alu.py
	.venv/bin/python scripts/test_immediate_alu.py

.PHONY: compare-emulators
compare-emulators:
	@test -n "$(REFERENCE_REPORT)" -a -n "$(CANDIDATE_REPORT)" || { echo 'Usage: make compare-emulators REFERENCE_REPORT=reports/reference.json CANDIDATE_REPORT=reports/candidate.json [DIFF_OUTPUT=reports/emulator-diff.json]'; exit 2; }
	python3 scripts/compare_emulator_runs.py "$(REFERENCE_REPORT)" "$(CANDIDATE_REPORT)" --output "$(if $(DIFF_OUTPUT),$(DIFF_OUTPUT),reports/emulator-diff.json)"
