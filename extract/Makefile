# this just obtains the directory this Makefile resides in
ROOT_DIR := $(shell cd $(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)));pwd)
CXX := clang++

CXXFLAGS := -O3 -g -Weverything -Wno-c++98-compat -Wno-shadow-field-in-constructor -Wno-padded -std=c++11
LDFLAGS  := -lboost_serialization -lboost_filesystem -lboost_system -lboost_program_options -lboost_context

CARBON_COLLECT_DIR      := $(ROOT_DIR)/../collect
CARBON_COLLECT_INCLUDES := -I $(CARBON_COLLECT_DIR)/include

SRCDIR     := src
BUILDDIR   := bin
INCLUDEDIR := include

SRCSNAMES = carbon_extract.cpp \
            link.cpp \
            toposort.cpp \
            reachable.cpp \
            code_reader.cpp \
            read_collection.cpp \
            graphviz.cpp \
            collection.cpp \
			static.cpp

SRCS := $(patsubst %,$(SRCDIR)/%,$(SRCSNAMES))
OBJS := $(patsubst %.cpp,$(BUILDDIR)/%.o,$(SRCSNAMES))
DEPS := $(patsubst %.cpp,$(BUILDDIR)/%.d,$(SRCSNAMES))

$(BUILDDIR)/carbon-extract: $(OBJS)
	@echo LD $(notdir $@)
	@$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS)

# pull in dependency info for existing .o files
-include $(DEPS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp Makefile
	@echo CXX $(notdir $@)
	@$(CXX) -o $@ -MMD -c $(CXXFLAGS) $(CARBON_COLLECT_INCLUDES) -I $(INCLUDEDIR) $<

.PHONY: clean
clean:
	rm -rf $(BUILDDIR)/*
