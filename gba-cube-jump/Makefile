DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

PREFIX := $(DEVKITARM)/bin/arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy

BUILD := build
TARGET := cubejump
SOURCES := $(wildcard source/*.c)
OBJECTS := $(patsubst source/%.c,$(BUILD)/%.o,$(SOURCES))
DEPENDS := $(OBJECTS:.o=.d)

CFLAGS := -mthumb -mthumb-interwork -mcpu=arm7tdmi -mtune=arm7tdmi \
	-O2 -Wall -Wextra -ffast-math -fomit-frame-pointer \
	-ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := -specs=gba.specs -Wl,--gc-sections

GAME_TITLE := CUBEJUMP
GAME_CODE := CJMP
MAKER_CODE := 8P

.PHONY: all clean

all: $(TARGET).gba

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: source/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(TARGET).gba: $(TARGET).elf tools/fix_gba_header.py
	$(OBJCOPY) -O binary $< $@
	python3 tools/fix_gba_header.py $@ "$(GAME_TITLE)" "$(GAME_CODE)" "$(MAKER_CODE)"

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).gba

-include $(DEPENDS)
