#ifndef DMABUF_H
#define DMABUF_H

typedef struct QemuDmaBuf QemuDmaBuf;

QemuDmaBuf *qemu_dmabuf_new(uint32_t width, uint32_t height,
                                   uint32_t stride, uint32_t x,
                                   uint32_t y, uint32_t backing_width,
                                   uint32_t backing_height, uint32_t fourcc,
                                   uint64_t modifier, int32_t dmabuf_fd,
                                   bool allow_fences, bool y0_top);
void qemu_dmabuf_free(QemuDmaBuf *dmabuf);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QemuDmaBuf, qemu_dmabuf_free);

int32_t qemu_dmabuf_get_fd(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_width(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_height(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_stride(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_fourcc(QemuDmaBuf *dmabuf);
uint64_t qemu_dmabuf_get_modifier(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_texture(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_x(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_y(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_backing_width(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_backing_height(QemuDmaBuf *dmabuf);
bool qemu_dmabuf_get_y0_top(QemuDmaBuf *dmabuf);
void *qemu_dmabuf_get_sync(QemuDmaBuf *dmabuf);
int32_t qemu_dmabuf_get_fence_fd(QemuDmaBuf *dmabuf);
bool qemu_dmabuf_get_allow_fences(QemuDmaBuf *dmabuf);
bool qemu_dmabuf_get_draw_submitted(QemuDmaBuf *dmabuf);
void qemu_dmabuf_set_texture(QemuDmaBuf *dmabuf, uint32_t texture);
void qemu_dmabuf_set_fence_fd(QemuDmaBuf *dmabuf, int32_t fence_fd);
void qemu_dmabuf_set_sync(QemuDmaBuf *dmabuf, void *sync);
void qemu_dmabuf_set_draw_submitted(QemuDmaBuf *dmabuf, bool draw_submitted);
void qemu_dmabuf_set_fd(QemuDmaBuf *dmabuf, int32_t fd);

#endif
