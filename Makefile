.PHONY: all networking xv6-test clean

all: networking

networking:
	$(MAKE) -C networking all

xv6-test:
	cd xv6 && python3 test-xv6.py cowtest && python3 test-xv6.py alarmtest

clean:
	$(MAKE) -C networking clean
	$(MAKE) -C xv6 clean
