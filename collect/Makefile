CXX := clang++

CXXFLAGS := -O3 -g -Weverything -Wno-c++98-compat -Wno-shadow-field-in-constructor -Wno-shadow-field-in-constructor -Wno-exit-time-destructors -Wno-global-constructors -Wno-switch-enum -Wno-padded -Wno-weak-vtables -fPIC
CXXFLAGS += $(filter-out -Wno-maybe-uninitialized,$(filter-out -fno-exceptions,$(filter-out -fno-rtti,$(shell llvm-config --cxxflags))))

LDFLAGS := -lboost_serialization -lboost_filesystem -lboost_system
LDFLAGS += $(shell llvm-config --ldflags --libs --system-libs)

SRCDIR     := src
BUILDDIR   := bin
INCLUDEDIR := include

SRCSNAMES := carbon_collect.cpp \
             collect.cpp \
             utilities_clang.cpp

SRCS := $(patsubst %,$(SRCDIR)/%,$(SRCSNAMES))
OBJS := $(patsubst %.cpp,$(BUILDDIR)/%.o,$(SRCSNAMES))
DEPS := $(patsubst %.cpp,$(BUILDDIR)/%.d,$(SRCSNAMES))

$(BUILDDIR)/carbon-collect.so: $(OBJS)
	@echo LD $(notdir $@)
	@$(CXX) -o $@ $(CXXFLAGS) $^ -shared $(LDFLAGS)

# pull in dependency info for existing .o files
-include $(DEPS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp Makefile
	@echo CXX $(notdir $@)
	@$(CXX) -o $@ -c -MMD $(CXXFLAGS) -I $(INCLUDEDIR) $<

.PHONY: clean
clean:
	rm -rf $(BUILDDIR)/*

.PHONY: install
install: $(BUILDDIR)/carbon-collect.so
	install $^ /usr/local/lib
