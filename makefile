# ============================
# Makefile — OSAL Linux + libgpiod (cross via SDK)
# ============================

# CC đến từ SDK (sau khi source environment-setup-*)
CC ?= $(CROSS_COMPILE)gcc

# Dirs
SRC_DIRS := src hal/src osal/src
INC_DIRS := include hal/include osal/include hw/include
OBJ_DIR  := out
TARGET   := osal_demo

# libgpiod flags (ưu tiên pkg-config của SDK; nếu không có thì fallback -I/-L)
GPIOD_CFLAGS := $(shell pkg-config --cflags gpiod 2>/dev/null)
GPIOD_LIBS   := $(shell pkg-config --libs   gpiod 2>/dev/null)
ifeq ($(strip $(GPIOD_LIBS)),)
  ifneq ($(strip $(SDKTARGETSYSROOT)),)
    GPIOD_CFLAGS += -I$(SDKTARGETSYSROOT)/usr/include
    GPIOD_LIBS   += -L$(SDKTARGETSYSROOT)/usr/lib -lgpiod
  else
    GPIOD_LIBS   += -lgpiod
  endif
endif

# Flags
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
CFLAGS  ?= -O2
CFLAGS  += -Wall -pthread $(INC_FLAGS) $(GPIOD_CFLAGS)
LDFLAGS ?=
LDFLAGS += -pthread $(GPIOD_LIBS)

# Debug (make DEBUG=1)
ifeq ($(DEBUG),1)
  CFLAGS += -g -DDEBUG
endif

# Sources & Objects
# SRCS := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
SRCS := src/gpio_daemon.c hal/src/hal_gpio_sim.c
# Ví dụ: src/app_linux.c  -> out/src/app_linux.o
#        osal/src/osal.c  -> out/osal/src/osal.o
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))

# Default
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	@echo "🔗 Linking $@ ..."
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compile (tự tạo thư mục out/ tương ứng)
$(OBJ_DIR)/%.o: %.c
	@echo "🧩 Compiling $< ..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Utilities
clean:
	@echo "🧹 Cleaning ..."
	rm -rf $(OBJ_DIR) $(TARGET)

run: $(TARGET)
	@echo "🚀 Running $(TARGET) ..."
	./$(TARGET)

.PHONY: all clean run

