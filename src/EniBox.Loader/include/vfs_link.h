#ifndef ENIBOX_VFS_LINK_H
#define ENIBOX_VFS_LINK_H
#include <stdint.h>

/* VfsLink 握手文件：父进程（封包产物）注入子进程前，把自己的镜像路径写到
 * 提取目录中的该文件；注入进非封包子进程的 Loader 在 DllMain 里读取它，
 * 映射父镜像的 .enibox 节，从而继承父的 VFS 视图。
 *
 * 布局（小端）:
 *   uint32 magic     = ENIBOX_VFSLINK_MAGIC
 *   uint32 pathChars = UTF-16 字符数（不含 NUL）
 *   WCHAR  path[pathChars]
 * 读写两端必须保持同步：写 = hook_process.c，读 = loader_main.c。 */
#define ENIBOX_VFSLINK_MAGIC  0x53465645u  /* 'EVFS' */
#define ENIBOX_VFSLINK_NAME   L"EniBox.VfsLink"

#endif
