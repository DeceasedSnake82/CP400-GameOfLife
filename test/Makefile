# Host-side tests (run on your PC / in CI, not on the calculator).
#
#   export SDK_DIR=/sdk         # same SDK the calculator build uses (headers only are needed)
#   make -C test engine         # engine vs naive reference + known Life results + benchmark
#   make -C test shots          # run the real UI headlessly, write screenshots to test/out/
#
SDK_DIR ?= /sdk
CXX     ?= g++
OUT     := out

ENGINE_SRC := test_engine.cpp ../life.cpp
APP_SRC    := host_app.cpp ../main.cpp ../gfx.cpp ../render.cpp ../life.cpp
APP_FLAGS  := -O2 -Wall -Wextra -DLIFE_HOST -Dmain=life_main -I$(SDK_DIR)/include -I..

.PHONY: engine render shots perf test clean

test: engine render

engine: $(OUT)/t_small $(OUT)/t_full
	$(OUT)/t_small
	$(OUT)/t_full bench

render: $(OUT)/t_render
	$(OUT)/t_render

$(OUT)/t_render: test_render.cpp ../render.cpp ../gfx.cpp ../life.cpp $(wildcard ../*.h) | $(OUT)
	$(CXX) -O2 -Wall -Wextra -DLIFE_HOST -I$(SDK_DIR)/include -I.. -o $@ test_render.cpp ../render.cpp ../gfx.cpp ../life.cpp

$(OUT)/t_small: $(ENGINE_SRC) ../life.h | $(OUT)
	$(CXX) -O2 -Wall -Wextra -DLIFE_TILES_LOG2=3 -o $@ $(ENGINE_SRC)

$(OUT)/t_full: $(ENGINE_SRC) ../life.h | $(OUT)
	$(CXX) -O2 -Wall -Wextra -o $@ $(ENGINE_SRC)

$(OUT)/app: $(APP_SRC) $(wildcard ../*.h) | $(OUT)
	$(CXX) $(APP_FLAGS) -o $@ $(APP_SRC)

shots: $(OUT)/app
	cd . && $(OUT)/app script.txt
	python3 ppm2png.py $(OUT)

perf: $(OUT)/app
	$(OUT)/app perf.txt

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
