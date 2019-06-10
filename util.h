/*
 * Copyright © 2019 Manuel Stoeckl
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef WAYPIPE_UTIL_H
#define WAYPIPE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef HAS_USDT
#include <sys/sdt.h>
#else
#define DTRACE_PROBE(provider, probe) (void)0
#define DTRACE_PROBE1(provider, probe, parm1) (void)0
#define DTRACE_PROBE2(provider, probe, parm1, parm2) (void)0
#define DTRACE_PROBE3(provider, probe, parm1, parm2, parm3) (void)0
#endif

// On SIGINT, this is set to true. The main program should then cleanup ASAP
extern bool shutdown_flag;

void handle_sigint(int sig);

/** Basic mathematical operations */
static inline int max(int a, int b) { return a > b ? a : b; }
static inline int min(int a, int b) { return a < b ? a : b; }
static inline int clamp(int v, int lower, int upper)
{
	return max(min(v, upper), lower);
}
static inline int align(int v, int m) { return m * ((v + m - 1) / m); }

/** Set the given flag with fcntl. Silently return -1 on failure. */
int set_fnctl_flag(int fd, int the_flag);
/** Create a nonblocking AF_UNIX/SOCK_STREAM socket, and listen with
 * nmaxclients. Prints its own error messages; returns -1 on failure. */
int setup_nb_socket(const char *socket_path, int nmaxclients);

enum compression_mode { COMP_NONE, COMP_LZ4, COMP_ZSTD };
int main_interface_loop(int chanfd, int progfd, const char *drm_node,
		enum compression_mode compression, bool no_gpu,
		bool display_side);

typedef enum { WP_DEBUG = 1, WP_ERROR = 2 } log_cat_t;

extern char waypipe_log_mode;
extern log_cat_t waypipe_loglevel;

void wp_log_handler(const char *file, int line, log_cat_t level,
		const char *fmt, ...);

#ifndef WAYPIPE_SRC_DIR_LENGTH
#define WAYPIPE_SRC_DIR_LENGTH 0
#endif
// no trailing ;, user must supply
#define wp_log(level, fmt, ...)                                                \
	if ((level) >= waypipe_loglevel)                                       \
	wp_log_handler(((const char *)__FILE__) + WAYPIPE_SRC_DIR_LENGTH,      \
			__LINE__, (level), fmt, ##__VA_ARGS__)

struct render_data {
	bool disabled;
	int drm_fd;
	const char *drm_node_path;
	struct gbm_device *dev;
};
struct fd_translation_map {
	struct shadow_fd *list;
	int max_local_id;
	int local_sign;
	enum compression_mode compression;
	struct render_data rdata;
};

typedef enum {
	FDC_UNKNOWN,
	FDC_FILE,
	FDC_PIPE_IR,
	FDC_PIPE_IW,
	FDC_PIPE_RW,
	FDC_DMABUF
} fdcat_t;
bool fdcat_ispipe(fdcat_t t);

struct pipe_buffer {
	void *data;
	ssize_t size;
	ssize_t used;
};

struct dmabuf_slice_data {
	/* This information partially duplicates that of a gbm_bo. However, for
	 * instance with weston, it is possible for the compositor to handle
	 * multibuffer multiplanar images, even though a driver may only support
	 * multiplanar images derived from a single underlying dmabuf. */
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t num_planes;
	// to which planes is the matching dmabuf assigned?
	uint8_t using_planes[4];
	uint32_t strides[4];
	uint32_t offsets[4];
	uint64_t modifier;
};

struct shadow_fd {
	struct shadow_fd *next; // singly-linked list
	fdcat_t type;
	int remote_id; // + if created serverside; - if created clientside
	int fd_local;
	// Dirty state.
	bool has_owner; // Are there protocol handlers which control the
			// is_dirty flag?
	bool is_dirty;  // If so, should this file be scanned for updates?
	/* Very simple damage tracking. This [min,max) interval contains all
	 * expected changes since the last synchronization. */
	int dirty_interval_min, dirty_interval_max;

	/* There are two types of reference counts for shadow_fd objects;
	 * a struct shadow_fd can only be safely deleted when both counts are
	 * zero. The protocol refcount tracks the number of protocol objects
	 * which have a reference to the shadow_fd (and which may try to
	 * mark it dirty.) The transfer refcount tracks the number of times
	 * that the object id (as either remote_id, or fd_local) must be passed
	 * on to the next program (waypipe instance, or application/compositor)
	 * so that said program can correctly parse its Wayland messages. */
	int refcount_protocol; // Number of references from protocol objects
	int refcount_transfer; // Number of references from fd transfer logic

	// common buffers for file-like types
	char *mem_mirror;      // exact mirror of the contents
	char *diff_buffer;     // target buffer for uncompressed diff
	char *compress_buffer; // target buffer for compressed diff
	size_t compress_space;

	// File data
	size_t file_size;
	char *file_mem_local; // mmap'd
	char file_shm_buf_name[256];

	// Pipe data
	struct pipe_buffer pipe_send;
	struct pipe_buffer pipe_recv;
	/* this is a pipe end we can read/write from. It only sometimes
	 * equals fd_local */
	int pipe_fd;
	bool pipe_readable, pipe_writable, pipe_onlyhere;
	// pipe closure (as in, POLLHUP, other end writeclose) -> statemachine?
	bool pipe_lclosed, pipe_rclosed;

	// DMAbuf data
	size_t dmabuf_size;
	struct gbm_bo *dmabuf_bo;
	struct dmabuf_slice_data dmabuf_info;
};

struct transfer {
	size_t size;
	fdcat_t type;
	int obj_id;
	// type-specific extra data
	union {
		int pipeclose;
		int file_actual_size;
		int raw; // < for obviously type-independent cases
	} special;
	// data vector must include space up to next 8-byte boundary
	const char *data;
};

void cleanup_translation_map(struct fd_translation_map *map);

/** Given a file descriptor, return which type code would be applied to its
 * shadow entry. (For example, FDC_PIPE_IR for a pipe-like object that can only
 * be read.) Sets *size if non-NULL and if the object is an FDC_FILE. */
fdcat_t get_fd_type(int fd, size_t *size);
/** Given a local file descriptor, produce matching global id, and register it
 * into the translation map if not already done. The function can also be
 * provided with optional extra information.
 */
struct dmabuf_slice_data;
struct shadow_fd *translate_fd(struct fd_translation_map *map, int fd,
		struct dmabuf_slice_data *info);
/** Given a struct shadow_fd, produce some number of corresponding file update
 * transfer messages. All pointers will be to existing memory. */
void collect_update(struct fd_translation_map *map, struct shadow_fd *cur,
		int *ntransfers, struct transfer transfers[]);
/** Apply a file update to the translation map, creating an entry when there is
 * none */
void apply_update(
		struct fd_translation_map *map, const struct transfer *transf);
/** Get the shadow structure associated to a remote id, or NULL if it dne */
struct shadow_fd *get_shadow_for_rid(struct fd_translation_map *map, int rid);

/** Count the number of pipe fds being maintained by the translation map */
int count_npipes(const struct fd_translation_map *map);
/** Fill in pollfd entries, with POLLIN | POLLOUT, for applicable pipe objects.
 * Specifically, if check_read is true, indicate all readable pipes.
 * Also, indicate all writeable pipes for which we also something to write. */
struct pollfd;
int fill_with_pipes(const struct fd_translation_map *map, struct pollfd *pfds,
		bool check_read);

/** mark pipe shadows as being ready to read or write */
void mark_pipe_object_statuses(
		struct fd_translation_map *map, int nfds, struct pollfd *pfds);
/** For pipes marked writeable, flush as much buffered data as possible */
void flush_writable_pipes(struct fd_translation_map *map);
/** For pipes marked readable, read as much data as possible without blocking */
void read_readable_pipes(struct fd_translation_map *map);
/** pipe file descriptors should never be removed, since then close-detection
 * fails. This closes the second pipe ends if we own both of them */
void close_local_pipe_ends(struct fd_translation_map *map);
/** If a pipe is remotely closed, but not locally closed, then close it too */
void close_rclosed_pipes(struct fd_translation_map *map);

/** Reduce the reference count for a shadow structure which is owned. The
 * structure should not be used by the caller after this point. Returns true if
 * pointer deleted. */
bool shadow_decref_protocol(struct fd_translation_map *map, struct shadow_fd *);
bool shadow_decref_transfer(struct fd_translation_map *map, struct shadow_fd *);
/** Increase the reference count of a shadow structure, and mark it as being
 * owned. For convenience, returns the passed-in structure. */
struct shadow_fd *shadow_incref_protocol(struct shadow_fd *);
struct shadow_fd *shadow_incref_transfer(struct shadow_fd *);
/** Decrease reference count for all objects in the given list, deleting
 * iff they are owned by protocol objects and have refcount zero */
void decref_transferred_fds(
		struct fd_translation_map *map, int nfds, int fds[]);
void decref_transferred_rids(
		struct fd_translation_map *map, int nids, int ids[]);

struct kstack {
	struct kstack *nxt;
	pid_t pid;
};

void wait_on_children(struct kstack **children, int options);

struct msg_handler {
	const struct wl_interface *interface;
	// these are structs packed densely with function pointers
	const void *event_handlers;
	const void *request_handlers;
};
struct wp_object {
	/* An object used by the wayland protocol. Specific types may extend
	 * this struct, using the following data as a header */
	const struct wl_interface *type; // Use to lookup the message handler
	uint32_t obj_id;
};

struct obj_list {
	struct wp_object **objs;
	int nobj;
	int size;
};
struct message_tracker {
	// objects all have a 'type'
	// creating a new type <-> binding it in the 'interface' list, via
	// registry. each type produces 'callbacks'
	struct obj_list objects;
};
struct context {
	struct message_tracker *const mt;
	struct fd_translation_map *const map;
	struct wp_object *obj;
	bool drop_this_msg;
	/* If true, running as waypipe client, and interfacing with compositor's
	 * buffers */
	const bool on_display_side;
	/* The transferred message can be rewritten in place, and resized, as
	 * long as there is space available. Setting 'fds_changed' will
	 * prevent the fd zone start from autoincrementing after running
	 * the function, which may be useful when injecting messages with fds */
	const int message_available_space;
	uint32_t *const message;
	int message_length;
	bool fds_changed;
	struct int_window *const fds;
};

struct char_window {
	char *data;
	int size;
	int zone_start;
	int zone_end;
};
struct int_window {
	int *data;
	int size;
	int zone_start;
	int zone_end;
};

// parsing.c

void listset_insert(struct obj_list *lst, struct wp_object *obj);
void listset_remove(struct obj_list *lst, struct wp_object *obj);
struct wp_object *listset_get(struct obj_list *lst, uint32_t id);

void init_message_tracker(struct message_tracker *mt);
void cleanup_message_tracker(
		struct fd_translation_map *map, struct message_tracker *mt);

/** Read message size from header; the 8 bytes beyond data must exist */
int peek_message_size(const void *data);
/**
 * The return value is false iff the given message should be dropped.
 * The flag `unidentified_changes` is set to true if the message does
 * not correspond to a known protocol.
 *
 * The message data payload may be modified and increased in size.
 *
 * The window `chars` should start at the message start, end
 * at its end, and indicate remaining space.
 * The window `fds` should start at the next fd in the queue, ends
 * with the last.
 *
 * The start and end of `chars` will be moved to the new end of the message.
 * The end of `fds` may be moved if any fds are inserted or discarded.
 * The start of fds will be moved, depending on how many fds were consumed.
 */
enum parse_state { PARSE_KNOWN, PARSE_UNKNOWN, PARSE_ERROR };
enum parse_state handle_message(struct message_tracker *mt,
		struct fd_translation_map *map, bool on_display_side,
		bool from_client, struct char_window *chars,
		struct int_window *fds);

// handlers.c
struct wl_interface;
struct wp_object *create_wp_object(
		uint32_t it, const struct wl_interface *type);
void destroy_wp_object(
		struct fd_translation_map *map, struct wp_object *object);
extern const struct msg_handler handlers[];
extern const struct wl_interface *the_display_interface;

// dmabuf.c

/* Additional information to help serialize a dmabuf */
int init_render_data(struct render_data *);
void cleanup_render_data(struct render_data *);
bool is_dmabuf(int fd);
struct gbm_bo *make_dmabuf(struct render_data *rd, const char *data,
		size_t size, struct dmabuf_slice_data *info);
int export_dmabuf(struct gbm_bo *bo);
struct gbm_bo *import_dmabuf(struct render_data *rd, int fd, size_t *size,
		struct dmabuf_slice_data *info);
void destroy_dmabuf(struct gbm_bo *bo);
void *map_dmabuf(struct gbm_bo *bo, bool write, void **map_handle);
int unmap_dmabuf(struct gbm_bo *bo, void *map_handle);
/** The handle values are unique among the set of currently active buffer
 * objects. To compare a set of buffer objects, produce handles in a batch, and
 * then free the temporary buffer objects in a batch */
int get_unique_dmabuf_handle(
		struct render_data *rd, int fd, struct gbm_bo **temporary_bo);
uint32_t dmabuf_get_simple_format_for_plane(uint32_t format, int plane);

// exported for testing
void apply_diff(size_t size, char *__restrict__ base, size_t diffsize,
		const char *__restrict__ diff);
void construct_diff(size_t size, size_t range_min, size_t range_max,
		char *__restrict__ base, const char *__restrict__ changed,
		size_t *diffsize, char *__restrict__ diff);

#endif // WAYPIPE_UTIL_H
