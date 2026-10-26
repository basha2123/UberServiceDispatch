CC ?= gcc
CXX ?= g++
BUILD_DIR ?= build
REPORT_OUTPUT_DIR := reports

CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -O2
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Werror -O2
THREAD_FLAGS := -pthread
TEST_LIBS := -lgtest

.PHONY: all integration test coverage valgrind cppcheck misra reports reports-dir clean

all: $(BUILD_DIR)/uber_dispatch

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

reports-dir:
	mkdir -p $(REPORT_OUTPUT_DIR)

$(BUILD_DIR)/uber_dispatch: uber_dispatch.c uber_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) uber_dispatch.c -o $@

$(BUILD_DIR)/uber_dispatch_test.o: uber_dispatch.c uber_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DUNIT_TEST=1 -c uber_dispatch.c -o $@

$(BUILD_DIR)/test_uber_dispatch: test_uber_dispatch.cpp uber_dispatch.h \
		$(BUILD_DIR)/uber_dispatch_test.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(THREAD_FLAGS) test_uber_dispatch.cpp \
		$(BUILD_DIR)/uber_dispatch_test.o $(TEST_LIBS) -o $@

integration: $(BUILD_DIR)/uber_dispatch | reports-dir
	printf '1\n2\nRider\n3\n4\n5\n6\n7\n8\n' | \
		(cd $(REPORT_OUTPUT_DIR) && ../$(BUILD_DIR)/uber_dispatch \
			> functional_poc_report.txt)
	grep -q 'Ride processed successfully' $(REPORT_OUTPUT_DIR)/functional_poc_report.txt
	grep -q 'Total leaked memory: 0 bytes' $(REPORT_OUTPUT_DIR)/functional_poc_report.txt
	printf 'invalid\n8\n' | (cd $(REPORT_OUTPUT_DIR) && \
		../$(BUILD_DIR)/uber_dispatch > invalid_menu_report.txt)
	grep -q 'Invalid Choice' $(REPORT_OUTPUT_DIR)/invalid_menu_report.txt
	: | (cd $(REPORT_OUTPUT_DIR) && \
		../$(BUILD_DIR)/uber_dispatch > eof_exit_report.txt)

test: $(BUILD_DIR)/test_uber_dispatch integration | reports-dir
	./$(BUILD_DIR)/test_uber_dispatch

$(BUILD_DIR)/uber_dispatch_coverage.o: uber_dispatch.c uber_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O0 --coverage -c uber_dispatch.c -o $@

$(BUILD_DIR)/uber_dispatch_test_coverage.o: uber_dispatch.c uber_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O0 --coverage -DUNIT_TEST=1 -c uber_dispatch.c -o $@

$(BUILD_DIR)/test_uber_dispatch_coverage: test_uber_dispatch.cpp uber_dispatch.h \
		$(BUILD_DIR)/uber_dispatch_test_coverage.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -O0 --coverage $(THREAD_FLAGS) test_uber_dispatch.cpp \
		$(BUILD_DIR)/uber_dispatch_test_coverage.o $(TEST_LIBS) -o $@

$(BUILD_DIR)/uber_dispatch_coverage: uber_dispatch.c uber_dispatch.h \
		$(BUILD_DIR)/uber_dispatch_coverage.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O0 --coverage $(THREAD_FLAGS) \
		$(BUILD_DIR)/uber_dispatch_coverage.o -o $@

coverage: $(BUILD_DIR)/test_uber_dispatch_coverage \
		$(BUILD_DIR)/uber_dispatch_coverage | reports-dir
	./$(BUILD_DIR)/test_uber_dispatch_coverage
	printf '1\n2\nRider\n3\n4\n5\n6\n7\n8\n' | \
		(cd $(REPORT_OUTPUT_DIR) && ../$(BUILD_DIR)/uber_dispatch_coverage \
			> coverage_poc_run.txt)
	printf 'invalid\n8\n' | (cd $(REPORT_OUTPUT_DIR) && \
		../$(BUILD_DIR)/uber_dispatch_coverage \
			> coverage_invalid_menu_run.txt)
	: | (cd $(REPORT_OUTPUT_DIR) && ../$(BUILD_DIR)/uber_dispatch_coverage \
		> coverage_eof_run.txt)
	lcov --capture --directory $(BUILD_DIR) \
		--output-file $(REPORT_OUTPUT_DIR)/coverage.info \
		--ignore-errors mismatch --branch-coverage
	lcov --extract $(REPORT_OUTPUT_DIR)/coverage.info '*/uber_dispatch.c' \
		--output-file $(REPORT_OUTPUT_DIR)/coverage_filtered.info \
		--branch-coverage
	lcov --summary $(REPORT_OUTPUT_DIR)/coverage_filtered.info --branch-coverage
	genhtml $(REPORT_OUTPUT_DIR)/coverage_filtered.info \
		--output-directory $(REPORT_OUTPUT_DIR)/coverage_html --branch-coverage
	gcovr --root . --filter 'uber_dispatch\.c$$' --txt \
		--txt-metric line --txt-metric branch \
		--html-details $(REPORT_OUTPUT_DIR)/gcovr_coverage.html \
		--fail-under-line 85 --fail-under-branch 80 $(BUILD_DIR)

valgrind: $(BUILD_DIR)/test_uber_dispatch $(BUILD_DIR)/uber_dispatch | reports-dir
	printf '1\n2\nRider\n3\n4\n5\n6\n7\n8\n' \
		> $(REPORT_OUTPUT_DIR)/valgrind_poc_input.txt
	valgrind --leak-check=full --show-leak-kinds=all \
		--errors-for-leak-kinds=all --error-exitcode=1 \
		--log-file=$(REPORT_OUTPUT_DIR)/valgrind_tests.txt \
		./$(BUILD_DIR)/test_uber_dispatch \
		> $(REPORT_OUTPUT_DIR)/valgrind_tests_output.txt
	(cd $(REPORT_OUTPUT_DIR) && valgrind --leak-check=full \
		--show-leak-kinds=all --errors-for-leak-kinds=all \
		--error-exitcode=1 --log-file=valgrind_poc.txt \
		../$(BUILD_DIR)/uber_dispatch < valgrind_poc_input.txt \
		> valgrind_poc_output.txt)
	cat $(REPORT_OUTPUT_DIR)/valgrind_tests.txt \
		$(REPORT_OUTPUT_DIR)/valgrind_poc.txt \
		> $(REPORT_OUTPUT_DIR)/valgrind_report.txt

cppcheck: | reports-dir
	: > $(REPORT_OUTPUT_DIR)/cppcheck_report.txt
	cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
		--suppress=missingIncludeSystem --inline-suppr --language=c --std=c11 \
		--template=gcc --output-file=$(REPORT_OUTPUT_DIR)/cppcheck_report.txt \
		uber_dispatch.c
	test ! -s $(REPORT_OUTPUT_DIR)/cppcheck_report.txt
	printf '%s\n' 'Cppcheck: PASS; no warnings or errors in uber_dispatch.c.' \
		>> $(REPORT_OUTPUT_DIR)/cppcheck_report.txt

misra: | reports-dir
	printf '%s\n' \
		'MISRA C:2012 Cppcheck addon scope: production C source uber_dispatch.c.' \
		'The GoogleTest suite is C++ and is not assessed as MISRA C.' \
		'This automated addon result is not formal MISRA certification.' \
		> $(REPORT_OUTPUT_DIR)/misra_report.txt
	cppcheck --enable=warning --error-exitcode=1 --addon=misra \
		--suppress=missingIncludeSystem --inline-suppr --language=c --std=c11 \
		--template=gcc --output-file=$(REPORT_OUTPUT_DIR)/misra_findings.txt \
		uber_dispatch.c
	cat $(REPORT_OUTPUT_DIR)/misra_findings.txt \
		>> $(REPORT_OUTPUT_DIR)/misra_report.txt
	test ! -s $(REPORT_OUTPUT_DIR)/misra_findings.txt
	printf '%s\n' 'MISRA C:2012 Cppcheck addon: PASS; zero findings.' \
		>> $(REPORT_OUTPUT_DIR)/misra_report.txt

reports: coverage valgrind cppcheck misra

clean:
	rm -rf $(BUILD_DIR) $(REPORT_OUTPUT_DIR)
