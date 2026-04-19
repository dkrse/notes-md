#ifndef SSH_H
#define SSH_H

#include <glib.h>
#include <gio/gio.h>

/* Check if a path refers to a remote SFTP mount */
gboolean ssh_path_is_remote(const char *path);

/* Convert local mount path to remote path */
const char *ssh_to_remote_path(const char *ssh_mount, const char *ssh_remote_path,
                               const char *local_path, char *buf, size_t buflen);

/* Build SSH base argv. Caller adds command args + trailing NULL, then g_ptr_array_unref(). */
GPtrArray *ssh_argv_new(const char *host, const char *user, int port,
                        const char *key, const char *ctl_path);

/* Start SSH ControlMaster for multiplexed connections */
void ssh_ctl_start(char *ctl_dir, size_t ctl_dir_size,
                   char *ctl_path, size_t ctl_path_size,
                   const char *host, const char *user, int port, const char *key);

/* Stop SSH ControlMaster and clean up socket */
void ssh_ctl_stop(char *ctl_path, char *ctl_dir,
                  const char *host, const char *user);

/* Run SSH command synchronously. Returns TRUE on success. */
gboolean ssh_spawn_sync(GPtrArray *argv, char **out_stdout, gsize *out_len);

/* Read remote file via SSH cat (binary-safe).
   Caller must g_free(*out_contents). */
gboolean ssh_cat_file(const char *host, const char *user, int port,
                      const char *key, const char *ctl_path,
                      const char *remote_path,
                      char **out_contents, gsize *out_len,
                      gsize max_file_size);

/* Write content to remote file via SSH.
   Returns TRUE on success. */
gboolean ssh_write_file(const char *host, const char *user, int port,
                        const char *key, const char *ctl_path,
                        const char *remote_path,
                        const char *content, gsize len);

#endif
