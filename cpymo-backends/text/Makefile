.PHONY: build run clean

BUILD_DIR := $(shell mkdir -p build)build
BUILD_DIR_CPYMO := $(shell mkdir -p $(BUILD_DIR)/cpymo)$(BUILD_DIR)/cpymo

OBJS := \
	$(patsubst %.c, $(BUILD_DIR)/%.o, $(wildcard *.c)) \
	$(patsubst %.c, $(BUILD_DIR_CPYMO)/%.o, $(notdir $(wildcard ../../cpymo/*.c)))

CFLAGS += \
	-DNDEBUG \
	-DDISABLE_AUDIO -DDISABLE_MOVIE \
	-DDISABLE_STB_IMAGE \
	-DLOW_FRAME_RATE \
	-O3

ifeq ($(LEAKCHECK), 1)
CFLAGS += -DLEAKCHECK
endif

LDFLAGS += -O3 -lm

# 本后端不实现 cpymo_backend_input_text_{begin,end,poll}，引擎侧靠符号地址为 NULL
# 判定「后端不支持文本输入」。ELF 的 ld 接受弱声明但从未定义的符号，ld64 不接受，
# 因此 macOS 上显式允许这三个可选钩子未定义（与 ELF 语义一致，地址解析为 0）。
ifeq ($(shell uname -s),Darwin)
LDFLAGS += \
	-Wl,-U,_cpymo_backend_input_text_begin \
	-Wl,-U,_cpymo_backend_input_text_end \
	-Wl,-U,_cpymo_backend_input_text_poll \
	-Wl,-undefined,dynamic_lookup
endif

ifeq ($(OS), Windows_NT)
OBJS += $(BUILD_DIR)/cpymo.res
LDFLAGS += --static -lmingw32
endif

TARGET := cpymo-text

build: $(TARGET)

run: build
	@./$(TARGET)

clean:
	@rm -rf build $(TARGET)

define compile
	@echo "$(notdir $1)"
	@$(CC) -c $1 -o $2 $(CFLAGS) 
endef

$(BUILD_DIR_CPYMO)/%.o: ../../cpymo/%.c
	$(call compile,$<,$@)

$(BUILD_DIR)/%.o: %.c
	$(call compile,$<,$@)

$(BUILD_DIR)/cpymo.res: ../sdl2/pymo-icon-windows.rc
	@windres -i $< -o $@ -O coff

$(TARGET): $(OBJS)
	@echo "Linking..."
	@$(CC) $^ -o $@ $(LDFLAGS)
	@echo "=> $@"
