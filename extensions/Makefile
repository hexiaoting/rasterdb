SUBDIRS=rasterdb

all clean distclean uninstall installcheck:
	for DIR in $(SUBDIRS); do \
                echo "---- Making $@ in $${DIR}"; \
                $(MAKE) -C "$${DIR}" $@ || exit 1; \
        done
install:
	for DIR in $(SUBDIRS); do \
                echo "---- Making $@ in $${DIR}"; \
                $(MAKE) -C "$${DIR}" $@ || exit 1; \
        done

