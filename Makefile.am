lib_LTLIBRARIES = liblpel.la

liblpel_la_SOURCES = \
	arch/atomic-pthread.h \
	arch/atomic-builtin.h \
	arch/atomic-x86.h \
	arch/atomic.h \
	arch/sysdep.h \
	arch/timing.h \
	arch/mctx.h \
	arch/mctx-pcl.h \
	arch/mctx-setjmp.h \
	arch/mctx-x86.h \
	arch/mctx-ucontext.h \
	mailbox.c \
	mailbox.h \
	modimpl/monitoring.c \
	modimpl/monitoring.h \
	buffer.c \
	buffer.h \
	scheduler.c \
	scheduler.h \
	stream.c \
	stream.h \
	streamset.c \
	streamset.h \
	task.c \
	task.h \
	taskqueue.c \
	taskqueue.h \
	lpel_main.c \
	lpel_main.h \
	lpel_name.h \
	lpel.h \
	worker.c \
	worker.h \
	workerctx.h \
	workermsg.h

if USE_MCTX_PCL
liblpel_la_LIBADD = $(LIBPCL_LA)
liblpel_la_CPPFLAGS = -DUSE_MTX_PCL
endif
if USE_MCTX_X86
liblpel_la_SOURCES += ctx/ctx_i386.S
endif
if USE_MCTX_X86_64
liblpel_la_SOURCES += ctx/ctx_amd64.S
endif
if USE_MCTX_X86_64_MEM
liblpel_la_SOURCES += ctx/ctx_amd64-mem.S
endif

ACLOCAL_AMFLAGS = -I build-aux