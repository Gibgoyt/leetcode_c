# lessons/Makefile
#
# `make N`        build lesson N -> lessons/N/main
# `make N-rt`     build with -fno-exceptions -fno-rtti -> lessons/N/main-rt
# `make run-N`    build and run lesson N
# `make clean-N`  remove lesson N's binaries
# `make all`      build every lesson that has a main.cpp
# `make clean`    remove every built binary
# `make help`     show available lessons + targets
#
# Lessons with multiple .cpp files just work: every .cpp under lessons/N/
# is compiled and linked into lessons/N/main.

CXX      := g++
STD      := -std=c++17
WARN     := -Wall -Wextra -Wpedantic -Werror \
            -Wshadow -Wold-style-cast -Wnon-virtual-dtor \
            -Wno-unused-function
DEBUG    := -g
OPT      := -O2
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
PTHREAD  := -pthread

CXXFLAGS := $(STD) $(WARN) $(DEBUG) $(OPT) $(SAN) $(PTHREAD)
LDFLAGS  := $(SAN) $(PTHREAD)

# Freestanding-flavoured flags. Use these for lesson 18 and to probe any
# earlier lesson with "what breaks if exceptions and RTTI go away?"
RT_FLAGS := -fno-exceptions -fno-rtti

# Every lesson directory with at least one main.cpp.
LESSONS := $(sort $(patsubst %/main.cpp,%,$(wildcard [0-9]*/main.cpp)))

.PHONY: all clean help compile_commands.json
.PHONY: $(LESSONS) $(addsuffix -rt,$(LESSONS)) \
        $(addprefix run-,$(LESSONS)) $(addprefix clean-,$(LESSONS))

all: $(LESSONS)

help:
	@echo "Lessons with source: $(LESSONS)"
	@echo ""
	@echo "Targets:"
	@echo "  make N           build lesson N           (./N/main)"
	@echo "  make N-rt        build freestanding-ish    (./N/main-rt)"
	@echo "  make run-N       build and run lesson N"
	@echo "  make clean-N     rm lesson N's binaries"
	@echo "  make all         build every lesson with source"
	@echo "  make clean       rm every built binary"
	@echo "  make compile_commands.json   needs 'bear'"

define LESSON_template
$(1): $(1)/main

$(1)/main: $$(wildcard $(1)/*.cpp)
	$$(CXX) $$(CXXFLAGS) $$^ -o $$@ $$(LDFLAGS)

$(1)-rt: $(1)/main-rt

$(1)/main-rt: $$(wildcard $(1)/*.cpp)
	$$(CXX) $$(CXXFLAGS) $$(RT_FLAGS) $$^ -o $$@ $$(LDFLAGS)

run-$(1): $(1)
	@echo "---- running lesson $(1) ----"
	@./$(1)/main

clean-$(1):
	@rm -f $(1)/main $(1)/main-rt
endef

$(foreach L,$(LESSONS),$(eval $(call LESSON_template,$(L))))

clean:
	@rm -f $(foreach L,$(LESSONS),$(L)/main $(L)/main-rt)
	@echo "cleaned"

compile_commands.json:
	@command -v bear >/dev/null 2>&1 || { echo "install 'bear' first"; exit 1; }
	@$(MAKE) clean
	bear -- $(MAKE) all
