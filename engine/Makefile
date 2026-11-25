CC      := gcc
BIN_DIR := build/bin
LIB_DIR := build/lib
SHARE_INSTALL := build/share
CFLAGS  := -Wall -Wmisleading-indentation -fno-common -I. -Ishared -Ishared/types
BUILD   ?= debug
# Keep compatibility workarounds enabled unless explicitly disabled.
COMPAT_WORKAROUNDS ?= 1
MSAA    ?= 0
GL_BACKEND ?= gl
WC3_REFORGED_COLORS ?= 1
GLSL      ?= 140

ifeq ($(BUILD),release)
	CFLAGS += -O2
else ifneq ($(BUILD),debug)
	$(error BUILD must be debug or release)
else
	CFLAGS += -O0 -g
endif
ifeq ($(filter $(COMPAT_WORKAROUNDS),0 1),)
	$(error COMPAT_WORKAROUNDS must be 0 or 1)
endif
CFLAGS += -DBZ_COMPAT_WORKAROUNDS=$(COMPAT_WORKAROUNDS)
ifeq ($(filter $(MSAA),0 2 4 8),)
	$(error MSAA must be 0, 2, 4, or 8)
endif
ifeq ($(filter $(GLSL),120 140 150),)
	$(error GLSL must be 120, 140, or 150)
endif
ifeq ($(GL_BACKEND),gles3)
	CFLAGS += -DBZ_GL_ES3
else ifeq ($(GL_BACKEND),gl)
	ifeq ($(GLSL),120)
		CFLAGS += -DBZ_GLSL_120
	else ifeq ($(GLSL),150)
		CFLAGS += -DBZ_GLSL_150
	endif
else
	$(error GL_BACKEND must be gl or gles3)
endif
ifeq ($(filter $(WC3_REFORGED_COLORS),0 1),)
	$(error WC3_REFORGED_COLORS must be 0 or 1)
endif
CFLAGS += -DWC3_REFORGED_COLORS=$(WC3_REFORGED_COLORS)
CFLAGS += -DBZ_MSAA_SAMPLES=$(MSAA)
ifneq ($(MSAA),0)
	CFLAGS += -DBZ_USE_MSAA
endif

ifeq ($(DIAG_OUTPUT),1)
	CFLAGS += -DDIAG_OUTPUT
endif
ifeq ($(NO_NETWORK),1)
	CFLAGS += -DBZ_NO_NETWORK
endif
ifeq ($(WC3_EARTHQUAKE_FRIENDLY_FIRE),1)
	CFLAGS += -DWC3_EARTHQUAKE_FRIENDLY_FIRE
endif
ifeq ($(WC3_DEBUG_MINING),1)
	CFLAGS += -DWC3_DEBUG_MINING
endif
ifeq ($(WC3_DEBUG_BLIGHT),1)
	CFLAGS += -DWC3_DEBUG_BLIGHT
endif
ifeq ($(WC3_DEBUG_CANNIBALIZE),1)
	CFLAGS += -DWC3_DEBUG_CANNIBALIZE
endif
ifeq ($(WC3_DEBUG_TUTORIAL_FLOW),1)
	CFLAGS += -DWC3_DEBUG_TUTORIAL_FLOW
endif
ifeq ($(DEBUG_MAP_LOAD),1)
	CFLAGS += -DDEBUG_MAP_LOAD
endif
# Include Warcraft III result/score investigation logging only when requested.
ifeq ($(WC3_DEBUG_SCORE),1)
	CFLAGS += -DBZ_WC3_DEBUG_SCORE
endif
# Use CascLib instead of the built-in MPQ reader (for CASC archives like SC2 retail).
# Requires CascLib headers and library: https://github.com/ladislav-zezula/CascLib
USE_CASC ?= 0
ifeq ($(USE_CASC),1)
	CFLAGS += -DHAVE_CASC
endif
# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
ifeq ($(EMSCRIPTEN),1)
    # Emscripten wasm build: static archives, SDL2/zlib ports, no shared libs/RPATH.
    CC        := emcc
    LIB_EXT   := .a
    LIB_FLAGS :=
    EXE_EXT   := .js
    INSTALL_NAME =
    RPATH     :=
    LIB_RPATH :=
    LDFLAGS   := -L$(LIB_DIR) -Wl,--allow-multiple-definition
    LIBS      :=
    NET_LIBS  :=
    EM_FS     ?= node
ifeq ($(EM_FS),browser)
    # The hosted game must load on WebKit releases that lack Memory64.
    CFLAGS    += -m32 -DBZ_WEB_ASYNC_ASSETS
    EM_FS_FLAGS := -sEXIT_RUNTIME=0 -lwebsocket.js -lidbfs.js \
                   -sASYNCIFY=1 -sASYNCIFY_IMPORTS=WC3_FetchRemoteFile -sASYNCIFY_STACK_SIZE=1048576 \
                   "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','stringToUTF8','FS','ENV','addRunDependency','removeRunDependency']"
else ifeq ($(EM_FS),node-async)
    EM_FS_FLAGS := -sASYNCIFY=1 -sASYNCIFY_IGNORE_INDIRECT=1 -sASYNCIFY_STACK_SIZE=4194304 -sNODERAWFS=1
else
    EM_FS_FLAGS := -sNODERAWFS=1
endif
ifneq ($(EM_FS),browser)
    CFLAGS    += -sMEMORY64=1
endif
    EM_PRELOAD ?=
    CFLAGS    += -sUSE_SDL=2 -sUSE_ZLIB=1
    EM_JS_LIBRARIES := port/web/src/web_beforeunload.js
    EM_LINK_FLAGS := -sSTACK_SIZE=8388608 -sALLOW_MEMORY_GROWTH=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 $(EM_FS_FLAGS) $(EM_PRELOAD) $(foreach lib,$(EM_JS_LIBRARIES),--js-library $(lib))
    AR        := $(shell command -v llvm-ar 2>/dev/null || command -v ar)
else ifeq ($(OS),Windows_NT)
	ifeq ($(GL_BACKEND),gles3)
		$(error GL_BACKEND=gles3 is currently supported on Linux only)
	endif
    LIB_EXT   := .dll
    LIB_FLAGS := -shared
    EXE_EXT   := .exe
    INSTALL_NAME =
    RPATH     :=
    LIB_RPATH :=
    LDFLAGS   := -L$(LIB_DIR)
	LIBS      := -lmingw32 -mwindows -lSDL2main -lSDL2 -lm -lepoxy -lopengl32 -lgdi32 -lws2_32
	NET_LIBS  := -lws2_32
else
    UNAME_S := $(shell uname -s)
    EXE_EXT :=
    ifeq ($(UNAME_S),Darwin)
		ifeq ($(GL_BACKEND),gles3)
			$(error GL_BACKEND=gles3 is currently supported on Linux only)
		endif
		ARCH ?= arm64
		ifeq ($(ARCH),arm64)
            HOMEBREW_PREFIX := /opt/homebrew
        else
            HOMEBREW_PREFIX := /usr/local
        endif
        LIB_EXT   := .dylib
        LIB_FLAGS := -dynamiclib
        # Set the dylib install name to @rpath/<libname> so the executable
        # resolves it via the @executable_path/../lib rpath, not a build path.
        INSTALL_NAME = -Wl,-install_name,@rpath/$(notdir $@)
        RPATH     := -Wl,-rpath,@executable_path/../lib
        LIB_RPATH := -Wl,-rpath,@loader_path
		CFLAGS    += -DGL_SILENCE_DEPRECATION -I$(HOMEBREW_PREFIX)/include -arch $(ARCH)
		LDFLAGS   := -L$(LIB_DIR) -L$(HOMEBREW_PREFIX)/lib -arch $(ARCH)
		LIBS      := -lSDL2 -framework AppKit -framework OpenGL
		NET_LIBS  :=
    else ifeq ($(UNAME_S),Linux)
        LIB_EXT   := .so
        LIB_FLAGS := -shared -fPIC
        INSTALL_NAME =
        # Executables resolve direct shared-lib deps via $ORIGIN/../lib. Shared
        # libraries need their own RUNPATH ($ORIGIN == this lib's dir) because
        # glibc does NOT inherit the executable's RUNPATH when resolving a
        # library's transitive deps (e.g. libgame.so -> libjass.so).
        RPATH     := -Wl,-rpath,'$$ORIGIN/../lib'
        LIB_RPATH := -Wl,-rpath,'$$ORIGIN'
        CFLAGS    += -fPIC
        LDFLAGS   := -L$(LIB_DIR) -Wl,-z,defs
		ifeq ($(GL_BACKEND),gles3)
			LIBS      := -lSDL2 -lEGL -lGLESv2 -lm
		else
			LIBS      := -lSDL2 -lEGL -lGL -lm
		endif
		NET_LIBS  :=
    else ifeq ($(UNAME_S),OpenBSD)
		ifeq ($(GL_BACKEND),gles3)
			$(error GL_BACKEND=gles3 is currently supported on Linux only)
		endif
        # BSD (OpenBSD): /usr/X11R6 for Mesa GL, clang as default CC
        ifeq ($(filter command line environment,$(origin CC)),)
            CC := clang
        else
        endif
        LIB_EXT   := .so
        LIB_FLAGS := -shared -fPIC
        INSTALL_NAME =
        # $ORIGIN doesn't work in RPATH/RUNPATH on OpenBSD
        RPATH     := -Wl,-rpath,$(abspath $(LIB_DIR))
        LIB_RPATH := -Wl,-rpath,$(abspath $(LIB_DIR))
        CFLAGS    += -fPIC -I/usr/local/include -I/usr/X11R6/include
        LDFLAGS   := -L$(LIB_DIR) -L/usr/local/lib -L/usr/X11R6/lib
		LIBS      := -lSDL2 -lGL -lm
		NET_LIBS  :=
    endif
endif

SHARED_LIB := $(LIB_DIR)/libshared$(LIB_EXT)

# Vendored Lua 5.4 (checked in under vendor/lua, compiled like Quake 3's jpeg).
# The interpreter/compiler drivers (lua.c, luac.c) are excluded; only the VM
# library is built, into a static archive linked by the WoW UI module.
LUA_DIR    := vendor/lua/src
LUA_SRCS   := $(filter-out $(LUA_DIR)/lua.c $(LUA_DIR)/luac.c, $(wildcard $(LUA_DIR)/*.c))
LUA_OBJ    := $(LIB_DIR)/lua.o
LUA_LIB    := $(LIB_DIR)/liblua.a
LUA_CFLAGS := -I$(LUA_DIR)
ifeq ($(UNAME_S),Linux)
LUA_CFLAGS += -DLUA_USE_LINUX
else ifeq ($(UNAME_S),Darwin)
LUA_CFLAGS += -DLUA_USE_MACOSX
endif

$(LUA_LIB): $(LUA_SRCS) $(wildcard $(LUA_DIR)/*.h) | $(LIB_DIR)
	@echo "[lua]"
	@$(call UNITY,$(LUA_DIR),! -name 'lua.c' ! -name 'luac.c') | \
		$(CC) $(CFLAGS) $(LUA_CFLAGS) -c -x c -o $(LUA_OBJ) -
	@$(AR) rcs $@ $(LUA_OBJ)

# The supplied map scripts use Lua 5.3.4. Keep its API and internals isolated
# from the independent Lua 5.4 UI dependency, including statically linked builds.
LUA53_DIR := vendor/lua53/src
LUA53_SRCS := $(filter-out $(LUA53_DIR)/lua.c $(LUA53_DIR)/luac.c, $(wildcard $(LUA53_DIR)/*.c))
LUA53_OBJ := $(LIB_DIR)/lua53.o
ERIS53_OBJ := $(LIB_DIR)/eris53.o
LUA53_LIB := $(LIB_DIR)/liblua53.a
LUA53_CFLAGS := -I$(LUA53_DIR) -Ivendor/eris53 -include vendor/lua53/namespace.h
LUA53_LIBS :=
ifneq ($(EMSCRIPTEN),1)
ifeq ($(UNAME_S),Linux)
LUA53_CFLAGS += -DLUA_USE_LINUX
LUA53_LIBS += -ldl
else ifeq ($(UNAME_S),Darwin)
LUA53_CFLAGS += -DLUA_USE_MACOSX
endif
endif

$(LUA53_LIB): $(LUA53_SRCS) $(wildcard $(LUA53_DIR)/*.h) vendor/lua53/namespace.h vendor/eris53/eris.c vendor/eris53/eris.h | $(LIB_DIR)
	@echo "[lua53]"
	@$(call UNITY,$(LUA53_DIR),! -name 'lua.c' ! -name 'luac.c') | \
		$(CC) $(CFLAGS) $(LUA53_CFLAGS) -c -x c -o $(LUA53_OBJ) -
	@$(CC) $(CFLAGS) $(LUA53_CFLAGS) -c vendor/eris53/eris.c -o $(ERIS53_OBJ)
	@$(AR) rcs $@ $(LUA53_OBJ) $(ERIS53_OBJ)

TOOL_SRCS := $(shell find tools -maxdepth 1 -name '*.c' ! -name 'jass.c' | sort)
TOOL_NAMES := $(patsubst tools/%.c,%,$(TOOL_SRCS))
TOOL_BINS := $(addprefix $(BIN_DIR)/,$(addsuffix $(EXE_EXT),$(TOOL_NAMES)))
ifeq ($(USE_CASC),1)
TOOL_DEPS := $(shell find tools -maxdepth 1 -name '*.h' | sort) common/casc.c common/mpq.h
else
TOOL_DEPS := $(shell find tools -maxdepth 1 -name '*.h' | sort) common/mpq.c common/mpq.h
endif
CLIENT_HEADERS := $(shell find client -name '*.h' | sort)
COMMON_HEADERS := $(shell find common -name '*.h' | sort)
# Included implementation fragments must invalidate unity-build dependents.
COMMON_HEADERS += common/script_save_impl.inc common/script_scope_binding.inc common/script_scope_checkpoint.inc
SERVER_HEADERS := $(shell find server -name '*.h' | sort)
WORLD_CORE_SRCS := common/world.c server/sv_routing.c server/routing.h
FONT_SRC := renderer/conchars.pcx
FONT_HEADER := renderer/conchars_sysfont.h
FONT_SYMBOL := conchars_sysfont_pcx

# Unity-build helper: pipe all .c files in a directory tree as #include
# directives to gcc's stdin so the whole module is one translation unit.
# Note: awk with octal \043 for '#' avoids Make treating '#' in a variable
# value as a comment character, which would truncate the sed expression.
UNITY = find $1 -name '*.c' $2 | sort | awk '{printf "\043include \"%s\"\n", $$0}'
CSRC = $(shell find $(1) -name '*.c' $(2) | sort)
COMMON_SRCS      := $(filter-out common/main.c, $(call CSRC,common))
ifeq ($(USE_CASC),1)
COMMON_GAME_SRCS := common/casc.c
else
COMMON_GAME_SRCS := common/mpq.c vendor/blast/blast.c
endif

define unity_lib_schema
$(1): $(2) $$(SERVER_HEADERS) | $$(LIB_DIR)
	@echo "[$(3)]"
	@$$(call UNITY,$(4),$(5)) | \
		$$(CC) $(6) $$(LIB_FLAGS) $$(INSTALL_NAME) $$(LIB_RPATH) -x c -o $$@ - $(7) $$(LDFLAGS) $(8)
endef

define src_lib_schema
$(1): $(2) $$(SERVER_HEADERS) | $$(LIB_DIR)
	@echo "[$(3)]"
	@$$(CC) $(4) $$(LIB_FLAGS) $$(INSTALL_NAME) $$(LIB_RPATH) -x c -o $$@ $(5) $$(LDFLAGS) $(6)
endef

define app_schema
$(1): $(2) $$(SERVER_HEADERS) $$(EM_APP_SRCS) $$(EM_JS_LIBRARIES) | $$(BIN_DIR) install-share
	@echo "[$(3)]"
	@$$(call UNITY,client server common sound $(6) $$(EM_APP_SRCS),! -name 'stb_vorbis.c' ! -name 'sv_routing.c') | \
		$$(CC) $(4) $$(EM_LINK_FLAGS) -x c -o $$@ - sound/stb_vorbis.c $$(RPATH) $$(LDFLAGS) $(5)
endef

ifeq ($(EMSCRIPTEN),1)
EM_APP_SRCS := port/web/src/web_gamepad.c port/web/src/web_loading.c port/web/src/web_chat.c
# Emscripten variants: modules are archived into static .a files (side modules
# would need runtime dlopen; a single-step static link keeps the wasm build simple).
define unity_lib_schema
$(1): $(2) $$(SERVER_HEADERS) | $$(LIB_DIR)
	@echo "[$(3)]"
	@$$(call UNITY,$(4),$(5)) | \
		$$(CC) $(6) -x c -c -o $(basename $(1)).o -
	@for extra in $(7); do \
		$$(CC) $(6) -c -o "$(LIB_DIR)/obj-$(3)-$$$${extra##*/}.o" "$$$$extra" || exit 1; \
	done
	@$$(AR) rcs $$@ $(basename $(1)).o $(foreach e,$(7),$(LIB_DIR)/obj-$(3)-$(notdir $e).o)
endef

define src_lib_schema
$(1): $(2) $$(SERVER_HEADERS) | $$(LIB_DIR)
	@echo "[$(3)]"
	@for f in $(5); do \
		$$(CC) $(4) -c -o "$(LIB_DIR)/obj-$(3)-$$$${f##*/}.o" "$$$$f" || exit 1; \
	done
	@$$(AR) rcs $$@ $(foreach f,$(5),$(LIB_DIR)/obj-$(3)-$(notdir $f).o)
endef
endif

# Arguments 2/5 are compile inputs; argument 8 contains runtime fixtures.
# Phony fixture checks must never make an otherwise current binary stale.
define test_schema

# Bootstrap dependency files for existing binaries once. Include only existing
# .d files so unrelated make targets do not build every test's dependencies.
.PHONY: $(4)-missing-deps
$(4): $(2) $(5) $$(SERVER_HEADERS) Makefile $(wildcard games/*/game.mk) $(if $(wildcard $(4).d),,$(4)-missing-deps) | $$(BIN_DIR)
	@$$(CC) $(3) -o $(4) $(5) $$(RPATH) $$(LDFLAGS) $(6)
	@$$(CC) $(3) -MM -MP -MT $(4) $(5) > $(4).d.tmp && mv $(4).d.tmp $(4).d

-include $(wildcard $(4).d)

$(1): $(4) $(8) | $$(TEST_JUNIT_DIR)
	@TEST_JUNIT="$$(TEST_JUNIT_DIR)/$(1).xml" TEST_JUNIT_SUITE="$(1)" $(4) $(7)
endef

# Track directories as well as files so fixture additions/removals invalidate
# archives. Depend on tool binaries, not their phony command aliases.
# Callers attach generation to the archive and validation to the phony alias.
define test_fixture_schema
.DELETE_ON_ERROR:
$(1): $(2)
$(2): $(if $(3),$(shell find $(3) -type f -o -type d | sort)) $(4) $(5)
endef

default: build
build: install-share
shared:      $(SHARED_LIB)

# Install project-owned read-only assets (engine fonts + per-game defaults) into
# the FHS-style build tree: build/bin/<exe>, build/lib/, build/share/<game>/.
# Mirrors the flat layout of a deployed portable app (share/ beside the exe).
install-share:
	@mkdir -p $(SHARE_INSTALL)
	@cp -R share/. $(SHARE_INSTALL)/
	@for g in warcraft-3; do \
		if [ -d games/$$g/share ]; then \
			mkdir -p $(SHARE_INSTALL)/$$g; \
			cp -R games/$$g/share/. $(SHARE_INSTALL)/$$g/; \
		fi; \
	done
	@echo "[install-share]"
tools:       $(TOOL_BINS)
	@echo "[tools]"
font:        $(FONT_HEADER)
$(TOOL_NAMES): %: $(BIN_DIR)/%$(EXE_EXT)

diag: clean
	$(MAKE) DIAG_OUTPUT=1 build
	$(MAKE) DIAG_OUTPUT=1 run

$(BIN_DIR) $(LIB_DIR):
	@mkdir -p $@

APP_SRCS          := $(filter-out server/sv_routing.c,$(shell find client server common sound -name '*.c'))
RENDERER_BASE_DEPS  := $(wildcard vendor/gl_shader/*.[ch]) $(SHARED_LIB) $(CLIENT_HEADERS) $(COMMON_HEADERS) $(COMMON_SRCS) $(FONT_HEADER)
ifeq ($(USE_CASC),1)
CASC_LIBS := -lcasc -lstdc++
else
CASC_LIBS :=
endif
RENDERER_SHARED_LIBS := -lshared $(LIBS) -lz $(CASC_LIBS)
SERVER_GAME_SRCS  := server/sv_quest.c
GAME_BASE_DEPS    := $(SHARED_LIB) $(COMMON_HEADERS) $(COMMON_SRCS) $(SERVER_GAME_SRCS)
UI_BASE_DEPS      := $(SHARED_LIB) $(CLIENT_HEADERS) $(COMMON_HEADERS)

$(eval $(call unity_lib_schema,$(SHARED_LIB),$(call CSRC,shared) vendor/blast/blast.c vendor/blast/blast.h,shared,shared,,$(CFLAGS),vendor/blast/blast.c,-lm))

TESTS_DIR := build/tests
TEST_JUNIT_DIR ?= $(TESTS_DIR)/junit

$(TEST_JUNIT_DIR):
	@mkdir -p $@

include games/warcraft-3/game.mk

$(BIN_DIR)/%$(EXE_EXT): tools/%.c $(TOOL_DEPS) $(CLIENT_HEADERS) $(COMMON_HEADERS) | $(BIN_DIR) $(SHARED_LIB) $(JASS_LIB) $(SHEET_LIB) $(RENDERER_LIB) $(GAME_LIB) $(MENU_LIB)
	@$(CC) $(CFLAGS) -o $@ $< \
		$(RPATH) $(LDFLAGS) -lsheet -lshared -ljass -lrenderer -lgame -lmenu $(LIBS) -lm -lz $(CASC_LIBS)

$(BIN_DIR)/img2sysfont$(EXE_EXT): tools/img2sysfont.c | $(BIN_DIR)
	@$(CC) $(CFLAGS) -o $@ tools/img2sysfont.c

$(BIN_DIR)/isoextract$(EXE_EXT): tools/isoextract.c | $(BIN_DIR)
	@$(CC) $(CFLAGS) -o $@ tools/isoextract.c

# Standalone native classic-WC3 Setup.mpq importer; no renderer or game libs.
$(BIN_DIR)/wc3setup$(EXE_EXT): tools/wc3setup.c common/mpq.c common/mpq.h vendor/blast/blast.c vendor/blast/blast.h | $(BIN_DIR)
	@$(CC) $(CFLAGS) -o $@ tools/wc3setup.c common/mpq.c vendor/blast/blast.c -lz

# imgdiff is standalone (stb only); it must NOT link the renderer, which also
# defines STB_IMAGE_IMPLEMENTATION (duplicate symbols otherwise).
$(BIN_DIR)/imgdiff$(EXE_EXT): tools/imgdiff.c renderer/stb/stb_image.h renderer/stb/stb_image_write.h | $(BIN_DIR)
	@echo "[imgdiff]"
	@$(CC) $(CFLAGS) -o $@ tools/imgdiff.c -lm

ifeq ($(EMSCRIPTEN),1)
# The font tool is a host binary; wasm builds expect the header pre-generated
# by the native build.
$(FONT_HEADER):
	@test -f $@ || { echo "missing $@ - generate with the native build first" >&2; exit 1; }
else
$(FONT_HEADER): $(FONT_SRC) $(BIN_DIR)/img2sysfont$(EXE_EXT)
	$(BIN_DIR)/img2sysfont$(EXE_EXT) $(FONT_SRC) $(FONT_HEADER) $(FONT_SYMBOL)
endif

clean:
	rm -rf build

.PHONY: default build shared tools font $(TOOL_NAMES) diag clean install-share $(WC3_PHONY)

# Browser foundation is isolated from native module linking and platform flags.
include port/web/build.mk

# Standalone production-shader probe; this is not an Android game build.
.PHONY: android-probe android-probe-run
android-probe:
	python3 tools/android/build_probe.py
android-probe-run:
	python3 tools/android/build_probe.py --serial $(or $(ANDROID_SERIAL),emulator-5554)

.PHONY: android-warcraft3 android-warcraft3-run
android-warcraft3:
	python3 tools/android/build_probe.py --game
android-warcraft3-run:
	python3 tools/android/build_probe.py --game --serial $(or $(ANDROID_SERIAL),emulator-5554)
