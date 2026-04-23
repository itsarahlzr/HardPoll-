.PHONY: all build test clean run-demo python-test

BUILD_DIR ?= build
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

all: build

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(JOBS)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

python-test:
	pytest

run-demo: build
	python3 -m hardpoll run --preset comparison --duration-ms=500 \
		--rate-hz=5000 --analyze

clean:
	rm -rf $(BUILD_DIR) runs
