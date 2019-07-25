/*
 * =====================================================================================
 *
 *       Filename:  ramfile.c
 *
 *    Description:  QEMUFile stored in dynamically allocated RAM for fast VMRestore
 *
 *         Author:  Alexander Oleinik (), alxndr@bu.edu
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "exec/memory.h"
#include "migration/qemu-file.h"
#include "migration/migration.h"
#include "migration/savevm.h"
#include "ramfile.h"

#define INCREMENT 10240
#define IO_BUF_SIZE 32768
#define MAX_IOV_SIZE MIN(IOV_MAX, 64)

struct QEMUFile {
    const QEMUFileOps *ops;
    const QEMUFileHooks *hooks;
    void *opaque;

    int64_t bytes_xfer;
    int64_t xfer_limit;

    int64_t pos; /* start of buffer when writing, end of buffer
                    when reading */
    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];

    DECLARE_BITMAP(may_free, MAX_IOV_SIZE);
    struct iovec iov[MAX_IOV_SIZE];
    unsigned int iovcnt;

    int last_error;
};

static ssize_t ram_writev_buffer(void *opaque, struct iovec *iov, int iovcnt,
                                  int64_t pos)
{
	ram_disk *rd = (ram_disk*)opaque;
	gsize newsize;
	ssize_t total_size = 0;
	int i;
	if(!rd->base) {
		rd->base = g_malloc(INCREMENT);
		rd->len = INCREMENT;
	}
	for(i = 0; i< iovcnt; i++)
	{
		if(pos+iov[i].iov_len >= rd->len ){
			newsize = ((pos + iov[i].iov_len)/INCREMENT + 1) * INCREMENT;
			rd->base = g_realloc(rd->base, newsize);
			rd->len = newsize;
		}
		/* for(int j =0; j<iov[i].iov_len; j++){ */
		/* 	printf("%hhx",*((char*)iov[i].iov_base+j)); */
		/* } */
		memcpy(rd->base + pos, iov[i].iov_base, iov[i].iov_len);
		pos += iov[i].iov_len;
		total_size += iov[i].iov_len;
	}
	return total_size;
}

static ssize_t ram_get_buffer(void *opaque, uint8_t *buf, int64_t pos,
                                   size_t size)
{
	ram_disk *rd = (ram_disk*)opaque;
	if(pos+size>rd->len){
		if(rd->len-pos>=0){
			memcpy(buf, rd->base + pos, rd->len-pos);
			size = rd->len-pos;
		}
	}
	else
		memcpy(buf, rd->base + pos, size);
	return size;
}

static int ram_fclose(void *opaque)
{
	return 0;
}

static const QEMUFileOps ram_read_ops = {
    .get_buffer = ram_get_buffer,
    .close =      ram_fclose
};

static const QEMUFileOps ram_write_ops = {
    .writev_buffer = ram_writev_buffer,
    .close =      ram_fclose
};

QEMUFile *qemu_fopen_ram(ram_disk **return_rd) {
	ram_disk *rd = g_new0(ram_disk, 1);
	*return_rd=rd;
	return qemu_fopen_ops(rd, &ram_write_ops);
}

QEMUFile *qemu_fopen_ro_ram(ram_disk* rd) {
    return qemu_fopen_ops(rd, &ram_read_ops);
}

void qemu_freopen_ro_ram(QEMUFile* f) {
	void *rd = f->opaque;
	f->bytes_xfer=0;
	f->xfer_limit=0;
	f->last_error=0;
	f->iovcnt=0;
	f->buf_index=0;
	f->buf_size=0;
	f->pos=0;
	f->ops = &ram_read_ops;
	f->opaque = rd;
	return;
}
