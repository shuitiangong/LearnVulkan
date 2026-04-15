.PHONY: run debug release _run-debug _run-release help

MODE := $(filter debug release,$(MAKECMDGOALS))

run:
ifeq ($(MODE),debug)
	@$(MAKE) --no-print-directory _run-debug
else ifeq ($(MODE),release)
	@$(MAKE) --no-print-directory _run-release
else
	@echo Usage: make run debug
	@echo        make run release
	@exit 1
endif

debug release:
ifeq ($(filter run,$(MAKECMDGOALS)),run)
	@:
else ifeq ($@,debug)
	@$(MAKE) --no-print-directory _run-debug
else
	@$(MAKE) --no-print-directory _run-release
endif

_run-debug:
	cmake --preset mingw64-debug
	cmake --build --preset build-mingw64-debug --target run

_run-release:
	cmake --preset mingw64-release
	cmake --build --preset build-mingw64-release --target run

help:
	@echo Available commands:
	@echo   make run debug
	@echo   make run release
	@echo   make debug
	@echo   make release
