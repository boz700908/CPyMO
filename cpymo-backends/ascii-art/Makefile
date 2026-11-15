.PHONY: build run clean

BUILD_DIR := $(shell mkdir -p build)build
BUILD_DIR_CPYMO := $(shell mkdir -p $(BUILD_DIR)/cpymo)$(BUILD_DIR)/cpymo
BUILD_DIR_CPYMO_BACKEND_SOFTWARE = $(shell mkdir -p $(BUILD_DIR)/cpymo_backend_software)$(BUILD_DIR)/cpymo_backend_software

OBJS := \
	$(patsubst %.c, $(BUILD_DIR)/%.o, $(wildcard *.c)) \
	$(patsubst %.c, $(BUILD_DIR_CPYMO)/%.o, $(notdir $(wildcard ../../cpymo/*.c))) \
	$(patsubst %.c, $(BUILD_DIR_CPYMO_BACKEND_SOFTWARE)/%.o, $(notdir $(wildcard ../software/*.c)))

INC := $(wildcard *.h) $(wildcard ../../cpymo/*.h) $(wildcard ../include/*.h) $(wildcard ../software/*.h)

CFLAGS += \
	-DDISABLE_AUDIO \
	-DDISABLE_MOVIE \
	-DNDEBUG \
	-O3

ifeq ($(LEAKCHECK), 1)
CFLAGS += -DLEAKCHECK
endif

ifeq ($(LUA), 1)
CFLAGS += -DCPYMO_FEATURE_LEVEL=1 -DLINK_WITH_ONELUA -I../../lua
endif

LDFLAGS += -g -lm

ifeq ($(shell uname -s),Darwin)
# software 后端同样没有实现 cpymo_backend_input_text_{begin,end,poll}。
# 即便头文件里把声明写成 weak_import，Mach-O 的 ld 仍不接受「已声明但从未定义」，
# 所以在 macOS 上显式允许这三个符号缺失（与 text 后端同一处理）。
LDFLAGS += \
	-Wl,-U,_cpymo_backend_input_text_begin \
	-Wl,-U,_cpymo_backend_input_text_end \
	-Wl,-U,_cpymo_backend_input_text_poll \
	-Wl,-undefined,dynamic_lookup
endif

TARGET := cpymo-ascii-art

build: $(TARGET)

run: build
	@./$(TARGET)

clean:
	@rm -rf build $(TARGET)

define compile
	@echo "$(notdir $1)"
	@$(CC) -c $1 -o $2 $(CFLAGS)
endef

$(BUILD_DIR_CPYMO_BACKEND_SOFTWARE)/%.o: ../software/%.c
	$(call compile,$<,$@)

$(BUILD_DIR_CPYMO)/%.o: ../../cpymo/%.c
	$(call compile,$<,$@)

$(BUILD_DIR)/%.o: %.c
	$(call compile,$<,$@)

$(BUILD_DIR)/cpymo.res: ../sdl2/pymo-icon-windows.rc
	@windres $< $@ -O coff

ifeq ($(OS), Windows_NT)
OBJS += $(BUILD_DIR)/cpymo.res
LDFLAGS += --static
endif

$(TARGET): $(OBJS) $(WINDOWS_RES)
	@echo "Linking..."
	@$(CC) $^ -o $@ $(LDFLAGS)
	@echo "=> $@"
