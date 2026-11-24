# Pi Games - top-level build. Plain make, plain C. See AGENTS.md "Build and verify".
#
#   make                      build the SDK, tools and every game          -> build/
#   make GAME=ball-pit        build only one game (GAMES="a b" for several)
#   make run GAME=ball-pit    run it natively (desktop window, or KMS/DRM on the Pi)
#   make check                the gate: lint + manifests + -Werror build + headless smoke + unit tests
#   make shot GAME=ball-pit   headless screenshot -> build/shots/ball-pit.png  (LOOK AT IT)
#   make icon GAME=ball-pit   regenerate games/ball-pit/icon.bmp from a headless run
#   make new GAME=my-game     copy games/_template to games/my-game
#   make perf                 headless benchmark table vs the AGENTS.md budgets
#   make PI=1 ...             tune for the Pi Zero 2 W (Cortex-A53)
#   make SAN=1 ...            AddressSanitizer + UBSan (recommended for check)
#   make WERROR=1 ...         warnings are errors (make check always does this)

BUILD     ?= build
CC        ?= cc
AR        ?= ar
GAMES_ALL := $(sort $(filter-out _%,$(notdir $(patsubst %/,%,$(dir $(wildcard games/*/game.ini))))))
GAMES     ?= $(if $(GAME),$(GAME),$(GAMES_ALL))
SMOKE_FRAMES ?= 300
SHOT_FRAMES  ?= 240

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

# -fno-math-errno lets sqrtf compile to a single instruction (big on ARM); -fno-trapping-math allows scheduling around FP ops.
OPT      ?= -O2 -g -fno-math-errno -fno-trapping-math
WARN     := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
ifeq ($(WERROR),1)
WARN     += -Werror
endif
# PI=1: tune for the Pi Zero 2 W (Cortex-A53). `tools/pg` sets it automatically when it runs on a Raspberry Pi.
ARCH     := $(shell $(CC) -dumpmachine 2>/dev/null)
PIFLAGS  :=
ifeq ($(PI),1)
ifneq (,$(findstring aarch64,$(ARCH)))
PIFLAGS  := -mcpu=cortex-a53
else ifneq (,$(findstring arm,$(ARCH)))
PIFLAGS  := -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
endif
endif
SANFLAGS :=
ifeq ($(SAN),1)
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
endif
CFLAGS   := -std=gnu11 $(OPT) $(PIFLAGS) $(WARN) $(SANFLAGS) -MMD -MP -Isdk $(SDL_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS  := $(SANFLAGS) $(EXTRA_LDFLAGS)
LDLIBS   := $(SDL_LIBS) -lm

SDK_SRCS := $(wildcard sdk/*.c)
SDK_OBJS := $(patsubst sdk/%.c,$(BUILD)/sdk/%.o,$(SDK_SRCS))
LIBPG    := $(BUILD)/sdk/libpg.a

.PHONY: all sdk tools games run check smoke validate lint test shot icon new perf clean help
all: sdk tools games

help:
	@sed -n '1,14p' Makefile

# ---------------------------------------------------------------- SDK
sdk: $(LIBPG)
$(LIBPG): $(SDK_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/sdk/%.o: sdk/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------- tools and tests
tools: $(BUILD)/tools/pgcat
$(BUILD)/tools/pgcat: tools/pgcat.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 $(OPT) $(WARN) $(SANFLAGS) -MMD -MP -Isdk $< -o $@ $(LDFLAGS)

# physics unit tests: no SDL needed
$(BUILD)/tests/phys_test: tests/phys_test.c sdk/pg_phys.c sdk/pg_phys.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 $(OPT) $(WARN) $(SANFLAGS) -Isdk tests/phys_test.c sdk/pg_phys.c -o $@ $(LDFLAGS) -lm

# ---------------------------------------------------------------- games
# A game is games/<id>/game.ini + games/<id>/src/*.c. No per-game Makefile: the build is shared on purpose.
define GAME_RULES
G_$(1)_SRCS := $$(wildcard games/$(1)/src/*.c games/$(1)/src/*/*.c)
G_$(1)_OBJS := $$(patsubst games/$(1)/src/%.c,$(BUILD)/games/$(1)/%.o,$$(G_$(1)_SRCS))
$(BUILD)/games/$(1)/$(1): $$(G_$(1)_OBJS) $(LIBPG)
	$$(CC) $$(LDFLAGS) -o $$@ $$(G_$(1)_OBJS) $(LIBPG) $$(LDLIBS)
$(BUILD)/games/$(1)/%.o: games/$(1)/src/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -Igames/$(1)/src -c $$< -o $$@
endef
$(foreach g,$(sort $(GAMES) $(GAMES_ALL)),$(eval $(call GAME_RULES,$(g))))

games: $(foreach g,$(GAMES),$(BUILD)/games/$(g)/$(g))

run: $(BUILD)/games/$(GAME)/$(GAME)
	@test -n "$(GAME)" || { echo "usage: make run GAME=<id>"; exit 2; }
	$(BUILD)/games/$(GAME)/$(GAME) $(ARGS)

# ---------------------------------------------------------------- verification
lint:
	@tools/lint.sh $(GAMES) $(if $(GAME),,_template)

validate: tools
	@$(BUILD)/tools/pgcat --root . validate $(GAMES)

test: $(BUILD)/tests/phys_test tools
	@$(BUILD)/tests/phys_test
	@if [ -x tools/selftest.sh ]; then tools/selftest.sh $(BUILD); fi

smoke: games
	@tools/smoke.sh $(BUILD) $(SMOKE_FRAMES) $(GAMES)

# check builds into its own directory (build/check, or build/check-san with SAN=1) so flags never mix
CHECKDIR := $(BUILD)/check$(if $(filter 1,$(SAN)),-san)
check:
	@$(MAKE) --no-print-directory lint
	@$(MAKE) --no-print-directory BUILD=$(CHECKDIR) WERROR=1 validate
	@$(MAKE) --no-print-directory BUILD=$(CHECKDIR) WERROR=1 all
	@$(MAKE) --no-print-directory BUILD=$(CHECKDIR) WERROR=1 test
	@$(MAKE) --no-print-directory BUILD=$(CHECKDIR) WERROR=1 smoke
	@echo "check: OK ($(GAMES))"

PERF_FRAMES ?= 900
perf: games
	@tools/perf.sh $(BUILD) $(PERF_FRAMES) $(GAMES)

shot: games
	@test -n "$(GAME)" || { echo "usage: make shot GAME=<id>"; exit 2; }
	@mkdir -p $(BUILD)/shots
	@t=tools/smoke.touch; [ -f games/$(GAME)/test.touch ] && t=games/$(GAME)/test.touch; \
	$(BUILD)/games/$(GAME)/$(GAME) --headless --frames $(SHOT_FRAMES) --touch-script $$t --seed 1 --screenshot $(BUILD)/shots/$(GAME).png
	@echo "screenshot: $(BUILD)/shots/$(GAME).png"

icon: games
	@test -n "$(GAME)" || { echo "usage: make icon GAME=<id>"; exit 2; }
	@t=tools/smoke.touch; [ -f games/$(GAME)/icon.touch ] && t=games/$(GAME)/icon.touch; \
	$(BUILD)/games/$(GAME)/$(GAME) --headless --frames $(SHOT_FRAMES) --touch-script $$t --seed 1 --icon games/$(GAME)/icon.bmp

new:
	@test -n "$(GAME)" || { echo "usage: make new GAME=<id>"; exit 2; }
	@echo "$(GAME)" | grep -Eq '^[a-z][a-z0-9-]{1,31}$$' || { echo "id must match [a-z][a-z0-9-]{1,31}"; exit 2; }
	@test ! -e games/$(GAME) || { echo "games/$(GAME) already exists"; exit 2; }
	cp -r games/_template games/$(GAME)
	rm -f games/$(GAME)/icon.bmp
	sed -i 's/^id *=.*/id = $(GAME)/' games/$(GAME)/game.ini
	sed -i 's/"_template"/"$(GAME)"/' games/$(GAME)/src/main.c
	@echo "created games/$(GAME) - now edit game.ini and src/main.c, then: make check GAME=$(GAME)"

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/sdk/*.d) $(wildcard $(BUILD)/games/*/*.d) $(wildcard $(BUILD)/games/*/*/*.d)
