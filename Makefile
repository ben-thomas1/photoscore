BUILD_DEBUG   := build/debug
BUILD_RELEASE := build/release

.PHONY: all debug release test test-release install clean

all: debug

# Debug is the strict-IEEE correctness reference: -O0, ASan + UBSan, -Werror.
debug: $(BUILD_DEBUG)/build.ninja
	meson compile -C $(BUILD_DEBUG)
	ln -sf $(BUILD_DEBUG)/compile_commands.json compile_commands.json

# Release validates the shipped -ffast-math / -march=native behaviour.
release: $(BUILD_RELEASE)/build.ninja
	meson compile -C $(BUILD_RELEASE)

$(BUILD_DEBUG)/build.ninja:
	meson setup $(BUILD_DEBUG) --buildtype=debug

$(BUILD_RELEASE)/build.ninja:
	meson setup $(BUILD_RELEASE) --buildtype=release

test: debug
	meson test -C $(BUILD_DEBUG) --print-errorlogs

test-release: release
	meson test -C $(BUILD_RELEASE) --print-errorlogs

install: release
	meson install -C $(BUILD_RELEASE)

clean:
	rm -rf build compile_commands.json
