.PHONY: all build qemu test host clean
all: build
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
test: build
	python3 scripts/run_desmume.py
host:
	python3 scripts/test_host.py
clean:
	rm -rf work artifacts suite.nds

.PHONY: sequential recovery
sequential: build
	python3 scripts/run_desmume.py --sequential --output reports/sequential.json --screenshot reports/score-screen.png
recovery: build
	python3 scripts/test_recovery.py

.PHONY: debug diagnostics
debug:
	python3 scripts/export_debug.py reports/resilient.json
diagnostics:
	python3 scripts/test_diagnostics.py
	python3 scripts/test_arm7_specific.py
	python3 scripts/test_privileged.py

.PHONY: qemu-exceptions milestone8
qemu-exceptions:
	python3 scripts/run_qemu_exceptions.py
milestone8:
	python3 scripts/test_milestone8.py

.PHONY: operand2
operand2:
	python3 scripts/test_operand2.py

.PHONY: compare2
compare2:
	python3 scripts/test_compare2.py

.PHONY: thumb-edges
thumb-edges:
	python3 scripts/run_qemu_thumb.py
	python3 scripts/test_thumb_edges.py

.PHONY: cpu-groups
cpu-groups:
	python3 scripts/test_cpu_groups.py

.PHONY: thumb-memory trace-provenance
thumb-memory:
	python3 scripts/run_qemu_thumb_memory.py
	python3 scripts/test_thumb_memory.py
trace-provenance:
	python3 scripts/test_trace_provenance.py

.PHONY: thumb-sp
thumb-sp:
	python3 scripts/run_qemu_thumb_sp.py
	python3 scripts/test_thumb_sp.py

.PHONY: thumb-pc
thumb-pc:
	python3 scripts/run_qemu_thumb_pc.py
	python3 scripts/test_thumb_pc.py
